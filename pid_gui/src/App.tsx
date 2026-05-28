import { useState, useEffect, useRef } from "react";
import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import "./App.css";

interface DataPoint {
  time: number;
  target: number;
  actual: number;
}

interface LogEntry {
  id: string;
  time: string;
  text: string;
  type: "info" | "success" | "warn" | "error";
}

interface ToastNotification {
  id: string;
  message: string;
  type: "success" | "error" | "info" | "warn";
}

function App() {
  // Notifications toast state
  const [notifications, setNotifications] = useState<ToastNotification[]>([]);

  const showNotification = (message: string, type: ToastNotification["type"] = "info") => {
    const id = Math.random().toString(36).substring(2, 9);
    const newNotif = { id, message, type };
    setNotifications((prev) => [...prev, newNotif]);
    
    // Tự động xóa thông báo sau 4 giây
    setTimeout(() => {
      setNotifications((prev) => prev.filter((n) => n.id !== id));
    }, 4000);
  };

  // Connection states
  const [ports, setPorts] = useState<string[]>([]);
  const [selectedPort, setSelectedPort] = useState<string>("");
  const [baudRate, setBaudRate] = useState<number>(115200);
  const [connected, setConnected] = useState<boolean>(false);
  const [loading, setLoading] = useState<boolean>(false);

  // Target Speed states (local UI values)
  const [targetRpm, setTargetRpm] = useState<number>(100);

  // Telemetry states (from device)
  const [actualRpm, setActualRpm] = useState<number>(0);
  const [deviceTargetRpm, setDeviceTargetRpm] = useState<number>(0);

  // Chart and Log states
  const [dataHistory, setDataHistory] = useState<DataPoint[]>([]);
  const [logs, setLogs] = useState<LogEntry[]>([]);

  // Advanced PID Config states (Default parameters tuned for dt=50ms)
  const [pidExpanded, setPidExpanded] = useState<boolean>(false);
  const [kp, setKp] = useState<number>(89.9360);
  const [ki, setKi] = useState<number>(228.4688);
  const [kd, setKd] = useState<number>(0.0614);


  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const dataHistoryRef = useRef<DataPoint[]>([]);

  // Keep ref up to date so event listener can append without stale closures
  useEffect(() => {
    dataHistoryRef.current = dataHistory;
  }, [dataHistory]);

  const addLog = (text: string, type: LogEntry["type"] = "info") => {
    const timeStr = new Date().toLocaleTimeString();
    const newEntry: LogEntry = {
      id: Math.random().toString(36).substring(2, 9),
      time: timeStr,
      text,
      type,
    };
    setLogs((prev) => [newEntry, ...prev].slice(0, 100));
  };

  // 1. Fetch available serial ports on mount and set status
  const refreshPorts = async () => {
    try {
      const available: string[] = await invoke("get_ports");
      setPorts(available);
      addLog(`Found ${available.length} active COM port(s).`, "info");
      if (available.length > 0 && !selectedPort) {
        setSelectedPort(available[0]);
      }
    } catch (err) {
      addLog(`Failed to scan COM ports: ${err}`, "error");
    }
  };

  const checkConnectionStatus = async () => {
    try {
      const activePort: string | null = await invoke("get_connection_status");
      if (activePort) {
        setConnected(true);
        setSelectedPort(activePort);
        addLog(`Already connected to ${activePort}.`, "success");
      }
    } catch (err) {
      console.error(err);
    }
  };

  useEffect(() => {
    refreshPorts();
    checkConnectionStatus();
  }, []);

  // 2. Listen to Tauri Events from Rust Serial Reader
  useEffect(() => {
    let active = true;
    let unlistenTelemetry: (() => void) | undefined;
    let unlistenStatus: (() => void) | undefined;
    let unlistenPid: (() => void) | undefined;

    async function setupListeners() {
      // Listen to telemetry updates from hardware
      const uTelemetry = await listen<{ target: number; actual: number }>(
        "telemetry",
        (event) => {
          if (!active) return;
          const { target, actual } = event.payload;
          setActualRpm(actual);
          setDeviceTargetRpm(target);

          // Append to chart history
          setDataHistory((prev) => {
            const next = [...prev, { time: Date.now(), target, actual }];
            if (next.length > 150) {
              next.shift();
            }
            return next;
          });
        }
      );
      if (!active) {
        uTelemetry();
        return;
      }
      unlistenTelemetry = uTelemetry;

      // Listen to hardware disconnects
      const uStatus = await listen<string>("serial-status", (event) => {
        if (!active) return;
        if (event.payload === "disconnected") {
          setConnected(false);
          addLog("USB connection was closed or lost.", "warn");
          showNotification("Mất kết nối cổng COM hoặc thiết bị bị rút ra!", "error");
        }
      });
      if (!active) {
        uStatus();
        return;
      }
      unlistenStatus = uStatus;

      // Listen to PID parameter updates from hardware
      const uPid = await listen<{ kp: number; ki: number; kd: number }>(
        "pid-params",
        (event) => {
          if (!active) return;
          const { kp, ki, kd } = event.payload;
          setKp(kp);
          setKi(ki);
          setKd(kd);
          addLog(`Sync PID parameters: Kp=${kp.toFixed(4)}, Ki=${ki.toFixed(4)}, Kd=${kd.toFixed(4)}`, "info");
          showNotification("Đã đồng bộ thông số PID từ STM32!", "info");
        }
      );
      if (!active) {
        uPid();
        return;
      }
      unlistenPid = uPid;
    }

    setupListeners();

    return () => {
      active = false;
      if (unlistenTelemetry) unlistenTelemetry();
      if (unlistenStatus) unlistenStatus();
      if (unlistenPid) unlistenPid();
    };
  }, []);

  // 3. Draw real-time Canvas graph when dataHistory updates
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    const width = canvas.width;
    const height = canvas.height;

    // Clear background
    ctx.fillStyle = "#0c101b";
    ctx.fillRect(0, 0, width, height);

    // Grid config
    ctx.strokeStyle = "#1e293b";
    ctx.lineWidth = 1;
    const cols = 10;
    const rows = 5;

    // Margins
    const padL = 60;
    const padR = 20;
    const padT = 20;
    const padB = 30;
    const chartW = width - padL - padR;
    const chartH = height - padT - padB;

    // Draw Grid Lines
    for (let i = 0; i <= cols; i++) {
      const x = padL + (chartW / cols) * i;
      ctx.beginPath();
      ctx.moveTo(x, padT);
      ctx.lineTo(x, height - padB);
      ctx.stroke();
    }
    for (let i = 0; i <= rows; i++) {
      const y = padT + (chartH / rows) * i;
      ctx.beginPath();
      ctx.moveTo(padL, y);
      ctx.lineTo(width - padR, y);
      ctx.stroke();
    }

    const data = dataHistory;
    if (data.length === 0) {
      ctx.fillStyle = "#6b7280";
      ctx.font = "14px 'Outfit', sans-serif";
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";
      ctx.fillText("Waiting for telemetry data... Connect to COM port.", width / 2, height / 2);
      return;
    }

    // Determine Y-axis max value dynamically
    let maxVal = 200;
    data.forEach((pt) => {
      if (pt.target > maxVal) maxVal = pt.target;
      if (pt.actual > maxVal) maxVal = pt.actual;
    });
    maxVal = Math.max(200, Math.ceil((maxVal + 20) / 50) * 50);

    // Draw Y-axis Labels
    ctx.fillStyle = "#9ca3af";
    ctx.font = "10px 'JetBrains Mono', monospace";
    ctx.textAlign = "right";
    ctx.textBaseline = "middle";
    for (let i = 0; i <= rows; i++) {
      const val = (maxVal / rows) * (rows - i);
      const y = padT + (chartH / rows) * i;
      ctx.fillText(`${val.toFixed(0)}`, padL - 8, y);
    }

    const getX = (idx: number) => {
      if (data.length <= 1) return padL;
      return padL + (idx / (data.length - 1)) * chartW;
    };
    const getY = (val: number) => {
      return height - padB - (val / maxVal) * chartH;
    };

    // Draw Target RPM Line (Cyan, Dashed)
    ctx.strokeStyle = "#06b6d4";
    ctx.lineWidth = 2;
    ctx.setLineDash([6, 4]);
    ctx.beginPath();
    data.forEach((pt, idx) => {
      const x = getX(idx);
      const y = getY(pt.target);
      if (idx === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    });
    ctx.stroke();
    ctx.setLineDash([]); // Reset

    // Draw Actual RPM Line (Purple, Solid, Glowing)
    ctx.strokeStyle = "#a855f7";
    ctx.lineWidth = 3;
    ctx.shadowColor = "rgba(168, 85, 247, 0.5)";
    ctx.shadowBlur = 8;
    ctx.beginPath();
    data.forEach((pt, idx) => {
      const x = getX(idx);
      const y = getY(pt.actual);
      if (idx === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    });
    ctx.stroke();
    ctx.shadowBlur = 0; // Reset shadow
  }, [dataHistory]);

  // 4. API Event handlers
  const handleConnect = async () => {
    if (!selectedPort) {
      addLog("Please select a COM port first.", "warn");
      showNotification("Vui lòng chọn cổng COM trước khi kết nối.", "warn");
      return;
    }
    setLoading(true);
    addLog(`Connecting to ${selectedPort} at ${baudRate} baud...`, "info");
    try {
      const res: string = await invoke("connect_port", {
        portName: selectedPort,
        baudRate: baudRate,
      });
      setConnected(true);
      addLog(res, "success");
      showNotification(`Kết nối thành công với ${selectedPort}!`, "success");

      // Yêu cầu thiết bị gửi lại thông số PID hiện tại đang lưu trong Flash/RAM
      setTimeout(async () => {
        try {
          await invoke("send_command", { cmd: "GET_PID" });
          addLog("Requested active PID parameters from device.", "info");
        } catch (e) {
          console.error("Failed to request active PID parameters:", e);
        }
      }, 300);
    } catch (err) {
      addLog(`Connection failed: ${err}`, "error");
      showNotification(`Kết nối thất bại: ${err}`, "error");
      setConnected(false);
    } finally {
      setLoading(false);
    }
  };

  const handleDisconnect = async () => {
    setLoading(true);
    addLog("Disconnecting device...", "info");
    try {
      const res: string = await invoke("disconnect_port");
      setConnected(false);
      setDataHistory([]);
      setActualRpm(0);
      setDeviceTargetRpm(0);
      addLog(res, "success");
      showNotification("Đã ngắt kết nối thiết bị.", "info");
    } catch (err) {
      addLog(`Disconnection failed: ${err}`, "error");
      showNotification(`Lỗi ngắt kết nối: ${err}`, "error");
    } finally {
      setLoading(false);
    }
  };

  const sendParam = async (cmd: string, valName: string, val: number) => {
    if (!connected) {
      addLog(`Cannot send ${valName}: Not connected.`, "warn");
      showNotification("Chưa kết nối cổng COM! Vui lòng chọn cổng và nhấn Kết nối trước.", "error");
      return;
    }
    try {
      await invoke("send_command", { cmd });
      addLog(`Sent command: ${cmd.trim()} (${valName} = ${val})`, "success");
    } catch (err) {
      addLog(`Failed to send ${valName}: ${err}`, "error");
    }
  };


  const applyTargetRpm = (val: number) => {
    const clamped = Math.min(170, Math.max(0, val));
    setTargetRpm(clamped);
    sendParam(`SP:${clamped.toFixed(1)}`, "Setpoint", clamped);
  };

  const handleNumberKeyDown = (e: React.KeyboardEvent<HTMLInputElement>) => {
    if (e.key === "Enter") {
      applyTargetRpm(targetRpm);
    }
  };

  const handleNumberBlur = () => {
    applyTargetRpm(targetRpm);
  };

  const handleStartMotor = async () => {
    if (!connected) {
      addLog("Cannot start motor: Not connected.", "warn");
      showNotification("Chưa kết nối cổng COM! Vui lòng chọn cổng và nhấn Kết nối trước.", "error");
      return;
    }
    try {
      // Set the setpoint first to make sure it's not zero (or matches UI)
      await invoke("send_command", { cmd: `SP:${targetRpm.toFixed(1)}` });
      addLog(`Sent command: SP:${targetRpm.toFixed(1)} (Setpoint = ${targetRpm})`, "success");
      
      // Then start the motor
      await invoke("send_command", { cmd: "START" });
      addLog("Sent command: START (Motor Run = 1)", "success");
      showNotification("Khởi động động cơ thành công!", "success");
    } catch (err) {
      addLog(`Failed to start motor: ${err}`, "error");
      showNotification(`Không thể khởi động động cơ: ${err}`, "error");
    }
  };

  const handleStopMotor = () => {
    if (!connected) {
      showNotification("Chưa kết nối cổng COM! Không thể dừng động cơ.", "error");
      return;
    }
    sendParam("STOP", "Motor Stop", 0);
    showNotification("Động cơ đã dừng.", "warn");
  };

  const clearLogs = () => {
    setLogs([]);
  };

  const handleSavePid = async () => {
    if (!connected) {
      addLog("Cannot save PID: Not connected.", "warn");
      showNotification("Chưa kết nối cổng COM! Vui lòng chọn cổng và nhấn Kết nối trước.", "error");
      return;
    }
    try {
      await invoke("send_command", { cmd: `KP:${kp.toFixed(4)}` });
      await invoke("send_command", { cmd: `KI:${ki.toFixed(4)}` });
      await invoke("send_command", { cmd: `KD:${kd.toFixed(4)}` });
      await invoke("send_command", { cmd: `APPLY_PID` });
      addLog(`Saved and Applied PID parameters: Kp=${kp.toFixed(4)}, Ki=${ki.toFixed(4)}, Kd=${kd.toFixed(4)}`, "success");
      showNotification("Đã lưu và áp dụng thông số PID thành công!", "success");
    } catch (err) {
      addLog(`Failed to save PID: ${err}`, "error");
      showNotification(`Lỗi khi lưu thông số PID: ${err}`, "error");
    }
  };

  const handleResetPid = async () => {
    const defaultKp = 89.9360;
    const defaultKi = 228.4688;
    const defaultKd = 0.0614;
    setKp(defaultKp);
    setKi(defaultKi);
    setKd(defaultKd);
    if (!connected) {
      addLog("Reset local PID values. Connect to board to apply them.", "info");
      showNotification("Đã reset thông số trên giao diện. Vui lòng kết nối cổng COM để áp dụng xuống STM32.", "warn");
      return;
    }
    try {
      await invoke("send_command", { cmd: `KP:${defaultKp.toFixed(4)}` });
      await invoke("send_command", { cmd: `KI:${defaultKi.toFixed(4)}` });
      await invoke("send_command", { cmd: `KD:${defaultKd.toFixed(4)}` });
      await invoke("send_command", { cmd: `APPLY_PID` });
      addLog(`Reset and Applied default PID parameters: Kp=${defaultKp}, Ki=${defaultKi}, Kd=${defaultKd}`, "success");
      showNotification("Đã khôi phục thông số PID mặc định thành công!", "success");
    } catch (err) {
      addLog(`Failed to reset PID on device: ${err}`, "error");
      showNotification(`Lỗi khi khôi phục thông số PID: ${err}`, "error");
    }
  };


  const errorVal = deviceTargetRpm - actualRpm;

  return (
    <div className="app-container">
      {/* HEADER SECTION */}
      <header className="header">
        <div className="brand-section">
          <svg
            className="logo-icon"
            xmlns="http://www.w3.org/2000/svg"
            fill="none"
            viewBox="0 0 24 24"
            stroke="currentColor"
            strokeWidth={2}
          >
            <path
              strokeLinecap="round"
              strokeLinejoin="round"
              d="M13 10V3L4 14h7v7l9-11h-7z"
            />
          </svg>
          <h1 className="brand-title">DC PID MOTOR MONITOR</h1>
        </div>

        <div className="connection-panel">
          <div className="status-badge">
            <span className={`status-dot ${connected ? "connected" : "disconnected"}`}></span>
            {connected ? "ONLINE" : "OFFLINE"}
          </div>

          <div className="select-wrapper">
            <select
              value={selectedPort}
              onChange={(e) => setSelectedPort(e.target.value)}
              disabled={connected}
            >
              {ports.length === 0 ? (
                <option value="">No ports found</option>
              ) : (
                ports.map((port) => (
                  <option key={port} value={port}>
                    {port}
                  </option>
                ))
              )}
            </select>
          </div>

          <div className="select-wrapper">
            <select
              value={baudRate}
              onChange={(e) => setBaudRate(Number(e.target.value))}
              disabled={connected}
            >
              <option value={9600}>9600 baud</option>
              <option value={19200}>19200 baud</option>
              <option value={38400}>38400 baud</option>
              <option value={57600}>57600 baud</option>
              <option value={115200}>115200 baud</option>
            </select>
          </div>

          <button
            onClick={refreshPorts}
            disabled={connected || loading}
            className="btn preset-btn"
            style={{ padding: "0.5rem 0.75rem", fontSize: "0.85rem" }}
          >
            Refresh
          </button>

          {connected ? (
            <button
              onClick={handleDisconnect}
              disabled={loading}
              className="btn btn-danger"
            >
              Disconnect
            </button>
          ) : (
            <button
              onClick={handleConnect}
              disabled={loading || ports.length === 0}
              className="btn btn-primary"
            >
              Connect
            </button>
          )}
        </div>
      </header>

      {/* DASHBOARD GRID */}
      <div className="dashboard-grid">
        {/* LEFT COLUMN: CONTROLS */}
        <aside className="card">
          <h2 className="card-title">Motor Control</h2>

          {/* Target Speed (Setpoint) Control */}
          <div className="control-group">
            <div className="setpoint-header">
              <span className="control-label">Setpoint (Target RPM)</span>
              <div className="setpoint-input-wrapper">
                <input
                  type="number"
                  className="setpoint-number-input"
                  min="0"
                  max="170"
                  value={targetRpm}
                  onChange={(e) => setTargetRpm(Number(e.target.value))}
                  onKeyDown={handleNumberKeyDown}
                  onBlur={handleNumberBlur}
                />
                <span className="unit-label">RPM</span>
              </div>
            </div>
            <div className="slider-container">
              <input
                type="range"
                className="setpoint-slider"
                min="0"
                max="170"
                step="5"
                value={targetRpm}
                onChange={(e) => setTargetRpm(Number(e.target.value))}
                onMouseUp={() => applyTargetRpm(targetRpm)}
                onTouchEnd={() => applyTargetRpm(targetRpm)}
              />
            </div>
          </div>



          {/* Start / Stop commands */}
          <div className="control-group" style={{ marginTop: "0.5rem" }}>
            <label className="control-label">Hardware Operations</label>
            <div className="motor-controls">
              <button onClick={handleStartMotor} className="btn btn-primary" style={{ background: "linear-gradient(135deg, #10b981, #059669)", boxShadow: "0 4px 12px rgba(16, 185, 129, 0.2)" }}>
                Start Motor
              </button>
              <button onClick={handleStopMotor} className="btn btn-danger">
                Stop Motor
              </button>
            </div>
          </div>

          {/* Advanced PID Configuration Collapsible */}
          <div className="pid-config-section">
            <button 
              className="pid-toggle-btn" 
              onClick={() => setPidExpanded(!pidExpanded)}
              style={{ width: "100%" }}
            >
              <span>Advanced PID Settings</span>
              <span>{pidExpanded ? "▲" : "▼"}</span>
            </button>
            {pidExpanded && (
              <div className="pid-inputs-panel">
                <div className="pid-input-group">
                  <label>Kp</label>
                  <input
                    type="number"
                    step="0.1"
                    value={kp}
                    onChange={(e) => setKp(Number(e.target.value))}
                  />
                </div>
                <div className="pid-input-group">
                  <label>Ki</label>
                  <input
                    type="number"
                    step="0.1"
                    value={ki}
                    onChange={(e) => setKi(Number(e.target.value))}
                  />
                </div>
                <div className="pid-input-group">
                  <label>Kd</label>
                  <input
                    type="number"
                    step="0.0001"
                    value={kd}
                    onChange={(e) => setKd(Number(e.target.value))}
                  />
                </div>
                <div className="pid-actions-row">
                  <button onClick={handleSavePid} className="btn-save-pid">Save</button>
                  <button onClick={handleResetPid} className="btn-reset-pid">Reset</button>
                </div>
              </div>
            )}
          </div>
        </aside>

        {/* RIGHT COLUMN: READOUTS & CHART */}
        <main style={{ display: "flex", flexDirection: "column", gap: "1.5rem" }}>
          {/* TELEMETRY DIGITAL READOUTS */}
          <div className="telemetry-row">
            <div className="tele-card">
              <span className="tele-label">Target RPM</span>
              <span className="tele-val target">
                {connected ? deviceTargetRpm.toFixed(0) : "---"}
              </span>
            </div>
            <div className="tele-card">
              <span className="tele-label">Actual RPM</span>
              <span className="tele-val actual">
                {connected ? actualRpm.toFixed(0) : "---"}
              </span>
            </div>
            <div className="tele-card">
              <span className="tele-label">Speed Error</span>
              <span className={`tele-val error`} style={{ color: Math.abs(errorVal) > 100 && connected ? "#ef4444" : "#f59e0b" }}>
                {connected ? errorVal.toFixed(0) : "---"}
              </span>
            </div>
          </div>

          {/* REAL TIME CHART GRAPH */}
          <div className="card">
            <h2 className="card-title">
              Real-time Response
              <div className="chart-legend">
                <div className="legend-item">
                  <span className="legend-color target"></span>
                  <span>Target RPM</span>
                </div>
                <div className="legend-item">
                  <span className="legend-color actual"></span>
                  <span>Actual RPM</span>
                </div>
              </div>
            </h2>
            <div className="chart-container">
              <canvas
                ref={canvasRef}
                className="chart-canvas"
                width={1600}
                height={640}
                style={{ width: "100%", height: "320px" }}
              />
            </div>
          </div>
        </main>
      </div>

      {/* BOTTOM SECTION: TERMINAL LOGS */}
      <footer className="card console-card">
        <h2 className="card-title" style={{ fontSize: "0.95rem", paddingBottom: "0.5rem" }}>
          System Log Output
          <button className="console-clear" onClick={clearLogs}>
            Clear Logs
          </button>
        </h2>
        <div className="console-output">
          {logs.length === 0 ? (
            <div className="console-line info">[Console initialized. Connect to a port to view activity.]</div>
          ) : (
            logs.map((log) => (
              <div key={log.id} className={`console-line ${log.type}`}>
                [{log.time}] {log.text}
              </div>
            ))
          )}
        </div>
      </footer>

      {/* Custom Notifications Toast Stack */}
      <div className="toast-container">
        {notifications.map((n) => (
          <div key={n.id} className={`toast-card ${n.type}`}>
            <div className="toast-icon">
              {n.type === "success" && "✓"}
              {n.type === "error" && "✗"}
              {n.type === "warn" && "⚠"}
              {n.type === "info" && "ℹ"}
            </div>
            <div className="toast-message">{n.message}</div>
            <button 
              className="toast-close-btn"
              onClick={() => setNotifications((prev) => prev.filter((item) => item.id !== n.id))}
            >
              ×
            </button>
          </div>
        ))}
      </div>
    </div>
  );
}

export default App;
