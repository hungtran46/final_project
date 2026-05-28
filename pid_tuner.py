#!/usr/bin/env python3
import sys
import time
import argparse
import serial
import serial.tools.list_ports
import numpy as np
from scipy.optimize import minimize
import matplotlib.pyplot as plt

class PIDTuner:
    def __init__(self, port, baudrate, setpoint, test_duration):
        self.port = port
        self.baudrate = baudrate
        self.setpoint = setpoint
        self.test_duration = test_duration
        self.serial = None
        self.iteration = 0
        self.cost_history = []
        self.param_history = []
        self.best_cost = float('inf')
        self.best_params = None
        self.best_response = None
        
        # Setup real-time plotting
        plt.ion()
        self.fig, (self.ax1, self.ax2) = plt.subplots(2, 1, figsize=(10, 8))
        self.fig.suptitle("DC Motor PID Auto-Tuning (Nelder-Mead Optimization)", fontsize=14, fontweight='bold')
        
        # Plot 1: Step Response
        self.ax1.set_title("Real-time Step Response")
        self.ax1.set_xlabel("Time (s)")
        self.ax1.set_ylabel("Speed (RPM)")
        self.ax1.grid(True, linestyle='--', alpha=0.6)
        
        # Plot 2: Cost History
        self.ax2.set_title("Optimization Convergence")
        self.ax2.set_xlabel("Iteration")
        self.ax2.set_ylabel("Cost Value")
        self.ax2.grid(True, linestyle='--', alpha=0.6)
        
        self.fig.tight_layout(rect=[0, 0.03, 1, 0.95])

    def connect(self):
        print(f"Connecting to {self.port} at {self.baudrate} baud...")
        try:
            self.serial = serial.Serial(self.port, self.baudrate, timeout=0.1)
            time.sleep(1.0) # Wait for connection to stabilize
            self.serial.reset_input_buffer()
            print("Connected successfully.")
        except Exception as e:
            print(f"Error connecting to serial port: {e}")
            sys.exit(1)

    def disconnect(self):
        if self.serial and self.serial.is_open:
            self.send_command("STOP")
            self.serial.close()
            print("Disconnected serial port.")

    def send_command(self, cmd):
        if not self.serial:
            return
        # Ensure newline is present
        if not cmd.endswith('\n'):
            cmd += '\n'
        self.serial.write(cmd.encode('ascii'))
        self.serial.flush()
        time.sleep(0.01) # Small delay for STM32 processing

    def run_step_test(self, kp, ki, kd):
        # 1. Dừng motor, đưa setpoint về 0
        self.send_command("STOP")
        self.send_command(f"SP:0")
        time.sleep(0.6) # Chờ motor dừng hoàn toàn
        
        # Flush buffers
        self.serial.reset_input_buffer()
        
        print(f"Testing parameters: Kp={kp:.3f}, Ki={ki:.3f}, Kd={kd:.3f}")
        
        # 2. Gửi KP/KI/KD riêng lẻ (buffered), sau đó APPLY_PID để apply đồng thời
        #    APPLY_PID cũng tự động reset integral → test bắt đầu sạch
        self.send_command(f"KP:{kp:.4f}")
        self.send_command(f"KI:{ki:.4f}")
        self.send_command(f"KD:{kd:.4f}")
        self.send_command("APPLY_PID")
        time.sleep(0.05)
        
        # 3. Trigger step change
        times = []
        actuals = []
        targets = []
        
        self.send_command(f"SP:{self.setpoint}")
        self.send_command("START")
        
        # 4. Đọc telemetry loop
        test_start = time.time()
        while (time.time() - test_start) < self.test_duration:
            line = self.serial.readline()
            if not line:
                continue
            
            try:
                line_str = line.decode('utf-8', errors='ignore').strip()
                if line_str.startswith("RPM:"):
                    parts = line_str[4:].split(',')
                    if len(parts) == 2:
                        target = float(parts[0])
                        actual = float(parts[1])
                        current_time = time.time() - test_start
                        
                        times.append(current_time)
                        targets.append(target)
                        actuals.append(actual)
            except Exception:
                pass
                
        # 5. Dừng motor sau mỗi test run
        self.send_command("STOP")
        self.send_command(f"SP:0")
        
        return np.array(times), np.array(targets), np.array(actuals)

    def evaluate_performance(self, times, targets, actuals, sp):
        if len(times) < 5:
            return 999999.0, 999999.0, self.test_duration, 999999.0
        
        errors = targets - actuals
        dt = np.diff(times, prepend=0.0)
        
        # 1. ITAE (Integral of Time-weighted Absolute Error)
        itae = np.sum(times * np.abs(errors) * dt)
        
        # 2. Overshoot percentage
        max_actual = np.max(actuals)
        overshoot = max(0.0, (max_actual - sp) / sp)
        
        # 3. Settling Time (thời gian vào và ở trong dải ±5% setpoint)
        tolerance = 0.05 * sp
        settling_time = self.test_duration  # Mặc định: chưa settle
        
        # Duyệt ngược: tìm thời điểm cuối cùng thoát khỏi dải dung sai
        for i in range(len(actuals) - 1, -1, -1):
            if abs(actuals[i] - sp) > tolerance:
                # Điểm này thoát dải → settling time là điểm kế tiếp
                settling_time = times[i + 1] if (i + 1) < len(times) else self.test_duration
                break
        else:
            # Vòng lặp hoàn thành không break → đã settle từ đầu
            settling_time = 0.0
            
        # 4. Steady-state error (15% cuối test)
        num_elements = len(actuals)
        idx_start = int(num_elements * 0.85)
        sse = np.mean(np.abs(errors[idx_start:])) if num_elements > 0 else 999.0
        
        return itae, overshoot, settling_time, sse

    def cost_function(self, params):
        self.iteration += 1
        kp, ki, kd = params
        
        # Constraint: không cho phép giá trị âm
        penalty = 0.0
        min_bound = 0.0
        if kp < min_bound:
            penalty += (min_bound - kp) * 10000.0
            kp = min_bound
        if ki < min_bound:
            penalty += (min_bound - ki) * 10000.0
            ki = min_bound
        if kd < min_bound:
            penalty += (min_bound - kd) * 10000.0
            kd = min_bound
            
        if penalty > 0.0:
            print(f"Iteration {self.iteration}: Parameters out of bounds. Penalty cost: {100000.0 + penalty:.2f}")
            return 100000.0 + penalty
        
        # --- Multi-setpoint testing ---
        # Test tại setpoint chính và 2 điểm làm việc khác (50% và 80%)
        # PID tốt phải hoạt động ổn định trên toàn dải tốc độ
        test_setpoints = [
            self.setpoint,           # Điểm tuning chính (trọng số 0.6)
            self.setpoint * 0.5,     # Tốc độ thấp 50% (trọng số 0.2)
            self.setpoint * 0.8,     # Tốc độ vừa 80% (trọng số 0.2)
        ]
        sp_weights = [0.6, 0.2, 0.2]
        
        total_cost = 0.0
        last_times, last_targets, last_actuals = None, None, None
        
        for sp, w in zip(test_setpoints, sp_weights):
            try:
                # Tạm thay setpoint để run_step_test dùng đúng giá trị
                orig_sp = self.setpoint
                self.setpoint = sp
                times, targets, actuals = self.run_step_test(kp, ki, kd)
                self.setpoint = orig_sp
            except Exception as e:
                print(f"Error during step test at SP={sp:.0f}: {e}")
                return 999999.0
            
            itae, overshoot, settling_time, sse = self.evaluate_performance(times, targets, actuals, sp)
            
            w_itae     = 0.8
            w_overshoot = 4000.0
            w_settling  = 800.0
            w_sse       = 5.0
            
            cost_sp = (w_itae * itae) + (w_overshoot * overshoot) + (w_settling * settling_time) + (w_sse * sse)
            total_cost += w * cost_sp
            
            if sp == self.setpoint or last_times is None:
                last_times, last_targets, last_actuals = times, targets, actuals
        
        cost = total_cost
        self.cost_history.append(cost)
        self.param_history.append(params)
        
        print(f"Run {self.iteration} Result -> Cost: {cost:.2f} (Kp={kp:.2f}, Ki={ki:.2f}, Kd={kd:.2f})")
        
        if cost < self.best_cost:
            self.best_cost = cost
            self.best_params = params.copy()
            self.best_response = (last_times, last_targets, last_actuals)
            
        self.update_plots(last_times, last_targets, last_actuals, kp, ki, kd)
        
        return cost

    def update_plots(self, times, targets, actuals, kp, ki, kd):
        # Clear plots for redrawing
        self.ax1.clear()
        self.ax1.set_title("Real-time Step Response")
        self.ax1.set_xlabel("Time (s)")
        self.ax1.set_ylabel("Speed (RPM)")
        self.ax1.grid(True, linestyle='--', alpha=0.6)
        
        # Draw current run
        self.ax1.plot(times, targets, 'r--', label='Target Setpoint', alpha=0.7)
        self.ax1.plot(times, actuals, color='#c084fc', linewidth=2, label=f'Current Run (Kp={kp:.2f}, Ki={ki:.2f}, Kd={kd:.2f})')
        
        # Draw best run so far
        if self.best_response is not None:
            b_times, _, b_actuals = self.best_response
            self.ax1.plot(b_times, b_actuals, color='#06b6d4', linewidth=2.5, 
                          label=f'Best Run (Cost={self.best_cost:.1f}, Kp={self.best_params[0]:.2f}, Ki={self.best_params[1]:.2f}, Kd={self.best_params[2]:.2f})')
            
        self.ax1.legend(loc='lower right')
        self.ax1.set_ylim(-100, self.setpoint * 1.3)
        
        # Draw Cost History
        self.ax2.clear()
        self.ax2.set_title("Optimization Convergence")
        self.ax2.set_xlabel("Iteration")
        self.ax2.set_ylabel("Cost Value")
        self.ax2.grid(True, linestyle='--', alpha=0.6)
        self.ax2.plot(range(1, len(self.cost_history)+1), self.cost_history, marker='o', color='#10b981', linewidth=2)
        
        # Highlight best iteration
        best_idx = np.argmin(self.cost_history) + 1
        self.ax2.plot(best_idx, self.best_cost, marker='*', color='gold', markersize=15, label=f'Best: iteration {best_idx}')
        self.ax2.legend()
        
        # Render changes
        self.fig.canvas.draw()
        self.fig.canvas.flush_events()
        plt.pause(0.1)

    def run_optimization(self, initial_guess, max_iterations):
        print("\n=== STARTING AUTOMATED TUNING PROCEDURE ===")
        print("Please stand back. The motor will begin cycles of step responses.")
        print(f"Tuning setpoint: {self.setpoint} RPM, test duration: {self.test_duration} seconds.")
        print(f"Initial PID Guess: Kp={initial_guess[0]}, Ki={initial_guess[1]}, Kd={initial_guess[2]}")
        print("===========================================\n")
        
        try:
            # Nelder-Mead algorithm
            result = minimize(
                self.cost_function,
                initial_guess,
                method='Nelder-Mead',
                options={
                    'maxiter': max_iterations,
                    'xatol': 0.05,
                    'fatol': 1.0,
                    'adaptive': True
                }
            )
            
            print("\n=== OPTIMIZATION COMPLETE ===")
            print(f"Status: {result.message}")
            print(f"Optimal Parameters Found:")
            print(f"  Kp = {self.best_params[0]:.4f}")
            print(f"  Ki = {self.best_params[1]:.4f}")
            print(f"  Kd = {self.best_params[2]:.4f}")
            print(f"Best Cost: {self.best_cost:.2f}")
            
            # Send final parameters to STM32 and save them
            print("\nApplying optimal parameters to the STM32...")
            self.send_command(f"KP:{self.best_params[0]:.4f}")
            self.send_command(f"KI:{self.best_params[1]:.4f}")
            self.send_command(f"KD:{self.best_params[2]:.4f}")
            self.send_command("STOP")
            print("Optimal parameters applied and motor stopped.")
            
            # Turn off interactive plotting and keep window open
            plt.ioff()
            
            # Save final graph to directory
            output_image = "tuning_result.png"
            self.fig.savefig(output_image)
            print(f"Tuning chart saved to '{output_image}'")
            
            print("\nShowing final results graph. Close the window to exit.")
            plt.show()
            
        except KeyboardInterrupt:
            print("\n\nTuning interrupted by user!")
            self.disconnect()
            sys.exit(0)

def detect_ports():
    ports = serial.tools.list_ports.comports()
    return [p.device for p in ports]

def main():
    parser = argparse.ArgumentParser(description="DC Motor PID Controller Auto-Tuning Script")
    parser.add_argument("--port", type=str, help="Serial port of STM32 (e.g. COM3 or /dev/ttyACM0). Auto-detected if empty.")
    parser.add_argument("--baud", type=int, default=115200, help="Baudrate of connection (default: 115200)")
    parser.add_argument("--setpoint", type=float, default=100.0, help="Tuning target speed in RPM (default: 100.0)")
    parser.add_argument("--duration", type=float, default=2.5, help="Step test duration in seconds (default: 2.5)")
    parser.add_argument("--max-iter", type=int, default=25, help="Maximum optimizer iterations (default: 25)")
    parser.add_argument("--kp-init", type=float, default=100.0, help="Initial Kp guess (default: 100.0)")
    parser.add_argument("--ki-init", type=float, default=50.0, help="Initial Ki guess (default: 50.0)")
    parser.add_argument("--kd-init", type=float, default=5.0, help="Initial Kd guess (default: 5.0)")
    
    args = parser.parse_args()
    
    # Auto-detect ports if none specified
    port = args.port
    if not port:
        available_ports = detect_ports()
        if len(available_ports) == 0:
            print("No active COM ports detected. Please connect your STM32 or specify the --port manually.")
            sys.exit(1)
        elif len(available_ports) == 1:
            port = available_ports[0]
            print(f"Auto-detected single COM port: {port}")
        else:
            print("Multiple COM ports detected:")
            for idx, p in enumerate(available_ports):
                print(f"  {idx+1}. {p}")
            choice = input(f"Select port (1-{len(available_ports)}): ").strip()
            try:
                port = available_ports[int(choice) - 1]
            except Exception:
                print("Invalid selection.")
                sys.exit(1)
                
    tuner = PIDTuner(port, args.baud, args.setpoint, args.duration)
    tuner.connect()
    
    initial_guess = [args.kp_init, args.ki_init, args.kd_init]
    tuner.run_optimization(initial_guess, args.max_iter)
    
    tuner.disconnect()

if __name__ == "__main__":
    main()
