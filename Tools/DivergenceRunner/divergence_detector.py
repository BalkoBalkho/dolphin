#!/usr/bin/env python3
"""
Dual-Instance Lockstep Divergence Detector for Dolphin Emulator.
Runs two Dolphin instances in Podman (or Host) with Null backend:
  - Instance A: JIT Core (CPUCore = 1 by default, or 6 for StencilInterpreter)
  - Instance B: Cached Interpreter Core (CPUCore = 5 by default)

Streams CPU state checkpoints (cycle, PC, GPRs, CR, XER, LR, CTR, MSR) over FIFOs
in lockstep. The instant a register or PC diverges, both instances are frozen,
and a rich divergence report with preceding execution history is produced.
"""

import argparse
import atexit
import collections
import os
import signal
import struct
import subprocess
import sys
import time
from pathlib import Path

# ANSI colors
RED = "\033[1;31m"
GREEN = "\033[1;32m"
YELLOW = "\033[1;33m"
CYAN = "\033[1;36m"
BOLD = "\033[1m"
RESET = "\033[0m"

# DivergenceRecord layout:
# u64 cycle
# u32 pc, msr, cr, xer, lr, ctr
# u32 gpr[32]
# Total: 8 + 6*4 + 32*4 = 160 bytes
RECORD_STRUCT = struct.Struct("<Q6I32I")
RECORD_SIZE = RECORD_STRUCT.size

class StateSnapshot:
    __slots__ = ("cycle", "pc", "msr", "cr", "xer", "lr", "ctr", "gpr")

    def __init__(self, raw_tuple):
        self.cycle = raw_tuple[0]
        self.pc = raw_tuple[1]
        self.msr = raw_tuple[2]
        self.cr = raw_tuple[3]
        self.xer = raw_tuple[4]
        self.lr = raw_tuple[5]
        self.ctr = raw_tuple[6]
        self.gpr = raw_tuple[7:39]

    def diff(self, other, ignored_registers=None):
        """Returns a list of (field_name, val_self, val_other) for mismatched registers."""
        ignored = set(ignored_registers or [])
        diffs = []
        if "PC" not in ignored and self.pc != other.pc:
            diffs.append(("PC", self.pc, other.pc))
        if "CR" not in ignored and self.cr != other.cr:
            diffs.append(("CR", self.cr, other.cr))
        if "XER" not in ignored and self.xer != other.xer:
            diffs.append(("XER", self.xer, other.xer))
        if "LR" not in ignored and self.lr != other.lr:
            diffs.append(("LR", self.lr, other.lr))
        if "CTR" not in ignored and self.ctr != other.ctr:
            diffs.append(("CTR", self.ctr, other.ctr))
        if "MSR" not in ignored and self.msr != other.msr:
            diffs.append(("MSR", self.msr, other.msr))
        for i in range(32):
            name = f"r{i}"
            if name not in ignored and self.gpr[i] != other.gpr[i]:
                diffs.append((name, self.gpr[i], other.gpr[i]))
        return diffs


def create_user_dir(path: Path, cpu_core: int):
    """Initializes an isolated Dolphin User directory with deterministic Null backend config."""
    config_dir = path / "Config"
    config_dir.mkdir(parents=True, exist_ok=True)

    dolphin_ini = config_dir / "Dolphin.ini"
    dolphin_ini.write_text(f"""[Core]
CPUCore = {cpu_core}
CPUThread = False
DSPThread = False
SyncGPU = True
EnableCustomRTC = True
CustomRTCValue = 1546300800
FastDiscSpeed = True
SyncOnSkipIdle = True
JITNoBlockLinking = False
JITFollowBranch = False

[DSP]
DSPHLE = True
DSPThread = False

[General]
ShowLag = False
ShowFrameCount = False
""")

    gfx_ini = config_dir / "GFX.ini"
    gfx_ini.write_text("""[Settings]
VideoBackend = Null
""")


class DivergenceRunner:
    def __init__(self, args):
        self.args = args
        self.work_dir = Path(f"/tmp/dolphin_div_{os.getpid()}_{int(time.time())}")
        self.work_dir.mkdir(parents=True, exist_ok=True)

        self.pipe_jit_path = self.work_dir / "jit.pipe"
        self.pipe_interp_path = self.work_dir / "interp.pipe"
        os.mkfifo(self.pipe_jit_path)
        os.mkfifo(self.pipe_interp_path)

        self.user_jit = self.work_dir / "user_jit"
        self.user_interp = self.work_dir / "user_interp"
        create_user_dir(self.user_jit, self.args.jit_core)
        create_user_dir(self.user_interp, self.args.interp_core)

        self.proc_jit = None
        self.proc_interp = None
        self.container_jit_name = f"dolphin_jit_{os.getpid()}"
        self.container_interp_name = f"dolphin_interp_{os.getpid()}"

        self.history_jit = collections.deque(maxlen=30)
        self.history_interp = collections.deque(maxlen=30)

        self.ignored_registers = set()
        if getattr(self.args, "ignore_regs", None):
            for r in self.args.ignore_regs.split(","):
                if r.strip():
                    self.ignored_registers.add(r.strip())
        self.auto_continue = getattr(self.args, "auto_continue", False)
        self.divergence_count = 0

        atexit.register(self.cleanup)

    def cleanup(self):
        print(f"\n{YELLOW}[DivergenceRunner] Cleaning up processes and containers...{RESET}")
        if self.args.mode == "podman":
            for name in (self.container_jit_name, self.container_interp_name):
                try:
                    subprocess.run(["podman", "kill", name], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    subprocess.run(["podman", "rm", "-f", name], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                except Exception:
                    pass
        else:
            for p in (self.proc_jit, self.proc_interp):
                if p and p.poll() is None:
                    try:
                        p.terminate()
                        p.wait(timeout=1)
                    except Exception:
                        p.kill()

        try:
            if self.pipe_jit_path.exists():
                self.pipe_jit_path.unlink()
            if self.pipe_interp_path.exists():
                self.pipe_interp_path.unlink()
        except Exception:
            pass

    def pause_instances(self):
        """Immediately pauses both instances to freeze state on divergence."""
        if self.args.mode == "podman":
            subprocess.run(["podman", "pause", self.container_jit_name], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            subprocess.run(["podman", "pause", self.container_interp_name], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        else:
            if self.proc_jit and self.proc_jit.poll() is None:
                os.kill(self.proc_jit.pid, signal.SIGSTOP)
            if self.proc_interp and self.proc_interp.poll() is None:
                os.kill(self.proc_interp.pid, signal.SIGSTOP)

    def resume_instances(self):
        """Resumes both instances after pausing."""
        if self.args.mode == "podman":
            subprocess.run(["podman", "unpause", self.container_jit_name], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            subprocess.run(["podman", "unpause", self.container_interp_name], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        else:
            if self.proc_jit and self.proc_jit.poll() is None:
                os.kill(self.proc_jit.pid, signal.SIGCONT)
            if self.proc_interp and self.proc_interp.poll() is None:
                os.kill(self.proc_interp.pid, signal.SIGCONT)

    def launch(self):
        d_binary = Path(self.args.dolphin_bin).resolve()
        exec_file = Path(self.args.executable).resolve()
        dolphin_root = Path(__file__).resolve().parents[2]

        print(f"{CYAN}{BOLD}==================================================================={RESET}")
        print(f"{CYAN}{BOLD}   DOLPHIN LOCKSTEP DIVERGENCE DETECTOR                           {RESET}")
        print(f"{CYAN}{BOLD}==================================================================={RESET}")
        print(f" Mode:            {BOLD}{self.args.mode.upper()}{RESET}")
        print(f" Target Game:     {BOLD}{exec_file}{RESET}")
        print(f" Instance A (JIT): Core {self.args.jit_core}")
        print(f" Instance B (Ref): Core {self.args.interp_core} (Cached Interpreter)")
        print(f" Trace Granularity: {'Basic Block' if self.args.block_trace else 'Timing Slice'}")
        print(f" Named Pipes:     {self.pipe_jit_path} & {self.pipe_interp_path}")
        print(f"{CYAN}-------------------------------------------------------------------{RESET}\n")

        block_env = "1" if self.args.block_trace else "0"

        if self.args.mode == "podman":
            cmd_jit = [
                "podman", "run", "--rm",
                "--name", self.container_jit_name,
                "--ipc=host",
                "-e", "HOME=/tmp",
                "-v", "/usr:/usr:ro",
                "-v", f"{dolphin_root}:/dolphin:ro",
                "-v", f"{exec_file.parent}:/game_dir:ro",
                "-v", f"{self.work_dir}:/div",
                "-e", "DOLPHIN_DIVERGENCE_PIPE=/div/jit.pipe",
                "-e", f"DOLPHIN_DIVERGENCE_BLOCK={block_env}",
                "docker.io/library/archlinux:latest",
                f"/dolphin/{d_binary.relative_to(dolphin_root)}",
                "-p", "headless", "-v", "Null",
                "-u", "/div/user_jit",
                "-e", f"/game_dir/{exec_file.name}"
            ]
            cmd_interp = [
                "podman", "run", "--rm",
                "--name", self.container_interp_name,
                "--ipc=host",
                "-e", "HOME=/tmp",
                "-v", "/usr:/usr:ro",
                "-v", f"{dolphin_root}:/dolphin:ro",
                "-v", f"{exec_file.parent}:/game_dir:ro",
                "-v", f"{self.work_dir}:/div",
                "-e", "DOLPHIN_DIVERGENCE_PIPE=/div/interp.pipe",
                "-e", f"DOLPHIN_DIVERGENCE_BLOCK={block_env}",
                "docker.io/library/archlinux:latest",
                f"/dolphin/{d_binary.relative_to(dolphin_root)}",
                "-p", "headless", "-v", "Null",
                "-u", "/div/user_interp",
                "-e", f"/game_dir/{exec_file.name}"
            ]
        else:
            # Host direct mode
            env_jit = os.environ.copy()
            env_jit["DOLPHIN_DIVERGENCE_PIPE"] = str(self.pipe_jit_path)
            env_jit["DOLPHIN_DIVERGENCE_BLOCK"] = block_env

            env_interp = os.environ.copy()
            env_interp["DOLPHIN_DIVERGENCE_PIPE"] = str(self.pipe_interp_path)
            env_interp["DOLPHIN_DIVERGENCE_BLOCK"] = block_env

            cmd_jit = [
                str(d_binary),
                "-p", "headless", "-v", "Null",
                "-u", str(self.user_jit),
                "-e", str(exec_file)
            ]
            cmd_interp = [
                str(d_binary),
                "-p", "headless", "-v", "Null",
                "-u", str(self.user_interp),
                "-e", str(exec_file)
            ]

        self.log_jit = open(self.work_dir / "jit.log", "w")
        self.log_interp = open(self.work_dir / "interp.log", "w")

        print(f"{YELLOW}[*] Launching Instance A (JIT)...{RESET}")
        self.proc_jit = subprocess.Popen(
            cmd_jit,
            stdout=self.log_jit,
            stderr=subprocess.STDOUT,
            env=(None if self.args.mode == "podman" else env_jit)
        )

        print(f"{YELLOW}[*] Launching Instance B (Reference Cached Interpreter)...{RESET}")
        self.proc_interp = subprocess.Popen(
            cmd_interp,
            stdout=self.log_interp,
            stderr=subprocess.STDOUT,
            env=(None if self.args.mode == "podman" else env_interp)
        )

        print(f"{GREEN}[*] Opening FIFO streams...{RESET}")
        # Note: opening FIFOs in read mode blocks until the writers open them.
        fd_jit = os.open(self.pipe_jit_path, os.O_RDONLY)
        fd_interp = os.open(self.pipe_interp_path, os.O_RDONLY)
        f_jit = os.fdopen(fd_jit, "rb", buffering=65536)
        f_interp = os.fdopen(fd_interp, "rb", buffering=65536)
        print(f"{GREEN}[*] Both instances connected! Monitoring lockstep execution...{RESET}\n")

        self.run_monitor(f_jit, f_interp)

    def run_monitor(self, f_jit, f_interp):
        count = 0
        t0 = time.time()
        last_report = t0
        last_count = 0

        while True:
            # Read exact 160-byte record from both
            buf_jit = f_jit.read(RECORD_SIZE)
            buf_interp = f_interp.read(RECORD_SIZE)

            if len(buf_jit) < RECORD_SIZE or len(buf_interp) < RECORD_SIZE:
                # One of the instances exited or crashed
                self.pause_instances()
                print(f"\n{YELLOW}[!] Stream closed (Instance terminated or reached EOF).{RESET}")
                if len(buf_jit) < RECORD_SIZE:
                    print(f"    Instance A (JIT) closed the pipe. Bytes: {len(buf_jit)}")
                if len(buf_interp) < RECORD_SIZE:
                    print(f"    Instance B (Interp) closed the pipe. Bytes: {len(buf_interp)}")
                print(f"    Total synchronised steps: {count:,}")
                break

            rec_jit = StateSnapshot(RECORD_STRUCT.unpack(buf_jit))
            rec_interp = StateSnapshot(RECORD_STRUCT.unpack(buf_interp))

            self.history_jit.append(rec_jit)
            self.history_interp.append(rec_interp)
            count += 1

            # Compare architectural state: PC + MSR + CR + XER + LR + CTR + 32 GPRs (offset 8 onward)
            diffs = rec_jit.diff(rec_interp, self.ignored_registers)
            if not diffs and self.args.strict_cycles:
                if rec_jit.cycle != rec_interp.cycle:
                    diffs.append(("cycle", rec_jit.cycle, rec_interp.cycle))

            if diffs:
                should_continue = self.handle_divergence(rec_jit, rec_interp, count, diffs)
                if not should_continue:
                    return

            now = time.time()
            if now - last_report >= 1.0:
                rate = (count - last_count) / (now - last_report)
                last_report = now
                last_count = count
                div_stat = f" | {YELLOW}Divergences: {self.divergence_count}{RESET}" if self.divergence_count > 0 else ""
                print(f"\r{GREEN}✓ Synced: {count:,} steps | Rate: {rate:,.0f} steps/s | PC: 0x{rec_jit.pc:08x}{div_stat}{RESET}", end="", flush=True)

            if self.args.max_steps and count >= self.args.max_steps:
                if self.divergence_count == 0:
                    print(f"\n\n{GREEN}[✓] Reached requested max steps ({self.args.max_steps:,}) with ZERO divergence!{RESET}")
                else:
                    print(f"\n\n{YELLOW}[✓] Reached requested max steps ({self.args.max_steps:,}) with {self.divergence_count} non-fatal divergence(s) logged.{RESET}")
                break

    def handle_divergence(self, jit: StateSnapshot, interp: StateSnapshot, step_idx: int, diffs: list) -> bool:
        self.divergence_count += 1
        mismatched_names = [d[0] for d in diffs]

        if self.auto_continue:
            print(f"\n{YELLOW}[Divergence #{self.divergence_count} @ step {step_idx:,}] PC: JIT=0x{jit.pc:08x} Ref=0x{interp.pc:08x} | Diff: {', '.join(mismatched_names)}{RESET}")
            return True

        self.pause_instances()
        print(f"\n\n{RED}{BOLD}╔═══════════════════════════════════════════════════════════════════════╗{RESET}")
        print(f"{RED}{BOLD}║                    DIVERGENCE DETECTED!                               ║{RESET}")
        print(f"{RED}{BOLD}╚═══════════════════════════════════════════════════════════════════════╝{RESET}")

        print(f"\n{BOLD}Step Index:{RESET}   {step_idx:,}")
        print(f"{BOLD}Cycle Count:{RESET}  JIT={jit.cycle:,} | INTERP={interp.cycle:,}")
        print(f"{BOLD}PC at Fault:{RESET}  JIT=0x{jit.pc:08x} | INTERP=0x{interp.pc:08x}")

        print(f"\n{BOLD}--- Register Differences ({len(diffs)} mismatched) ---{RESET}")
        print(f" {'Register':<10} {'JIT Value':<18} {'Reference (Interp)':<18} {'Diff Bits'}")
        print(f" {'-'*10} {'-'*18} {'-'*18} {'-'*12}")
        for name, val_jit, val_ref in diffs:
            xor_diff = val_jit ^ val_ref
            print(f" {RED}{name:<10} 0x{val_jit:08x}         0x{val_ref:08x}         ^0x{xor_diff:08x}{RESET}")

        # Preceding history trace
        print(f"\n{CYAN}{BOLD}--- Preceding Execution Trace (Last 10 steps) ---{RESET}")
        hist_len = min(len(self.history_jit), len(self.history_interp))
        start_idx = max(0, hist_len - 10)
        for i in range(start_idx, hist_len - 1):
            h_j = self.history_jit[i]
            h_i = self.history_interp[i]
            print(f"  [-{hist_len - 1 - i:>2}] PC=0x{h_j.pc:08x} (cycle {h_j.cycle:,}) r3=0x{h_j.gpr[3]:08x} r4=0x{h_j.gpr[4]:08x} lr=0x{h_j.lr:08x}")

        if hasattr(self, "log_jit") and self.log_jit:
            self.log_jit.flush()
            self.log_interp.flush()
            log_j_path = self.work_dir / "jit.log"
            log_i_path = self.work_dir / "interp.log"
            if log_j_path.exists():
                lines_j = [l.strip() for l in log_j_path.read_text().splitlines() if "ERROR" in l or "WARN" in l]
                if lines_j:
                    print(f"\n{YELLOW}Recent JIT logs:{RESET}")
                    for l in lines_j[-10:]:
                        print(f"  {l}")
            if log_i_path.exists():
                lines_i = [l.strip() for l in log_i_path.read_text().splitlines() if "ERROR" in l or "WARN" in l]
                if lines_i:
                    print(f"\n{YELLOW}Recent Interp logs:{RESET}")
                    for l in lines_i[-10:]:
                        print(f"  {l}")

        if sys.stdin.isatty():
            while True:
                try:
                    choice = input(f"\n{GREEN}[?] Action: [c]ontinue, [a]uto-continue all, [i]gnore reg, [q]uit: {RESET}").strip().lower()
                except (EOFError, KeyboardInterrupt):
                    return False
                if choice == 'c':
                    self.resume_instances()
                    return True
                elif choice == 'a':
                    self.auto_continue = True
                    self.resume_instances()
                    return True
                elif choice.startswith('i'):
                    parts = choice.split()
                    if len(parts) > 1:
                        for r in parts[1].split(','):
                            self.ignored_registers.add(r.strip())
                            print(f"{YELLOW}Added '{r.strip()}' to ignored registers.{RESET}")
                    else:
                        for d in diffs:
                            self.ignored_registers.add(d[0])
                        print(f"{YELLOW}Added {mismatched_names} to ignored registers.{RESET}")
                    self.resume_instances()
                    return True
                elif choice == 'q':
                    return False

        print(f"\n{YELLOW}Both instances have been PAUSED for inspection.{RESET}")
        if self.args.mode == "podman":
            print(f"To inspect or unpause: 'podman ps', 'podman logs {self.container_jit_name}'")
        print(f"{CYAN}{BOLD}==================================================================={RESET}\n")
        return False


def main():
    parser = argparse.ArgumentParser(description="Dolphin Lockstep Dual-Instance Divergence Detector")
    parser.add_argument("-e", "--executable", required=True, help="Path to DOL, ELF, or ISO")
    parser.add_argument("-b", "--dolphin-bin", default="build/Binaries/dolphin-emu-nogui", help="Path to dolphin-emu-nogui")
    parser.add_argument("--jit-core", type=int, default=1, help="CPUCore for Instance A (1=Jit64, 6=StencilInterpreter)")
    parser.add_argument("--interp-core", type=int, default=5, help="CPUCore for Instance B (5=CachedInterpreter, 0=Interpreter)")
    parser.add_argument("--mode", choices=["podman", "host"], default="podman", help="Execution mode (podman container or host)")
    parser.add_argument("--block-trace", action="store_true", help="Trace every basic block instead of timing slices")
    parser.add_argument("--strict-cycles", action="store_true", help="Assert exact match on cycle timer in addition to registers")
    parser.add_argument("-n", "--max-steps", type=int, default=None, help="Stop after N synchronized steps")
    parser.add_argument("--auto-continue", action="store_true", help="Log divergences and continue running without pausing")
    parser.add_argument("--ignore-regs", type=str, default="", help="Comma-separated register names to ignore (e.g. 'r4,r5')")

    args = parser.parse_args()
    runner = DivergenceRunner(args)
    runner.launch()


if __name__ == "__main__":
    main()
