use std::sync::{Mutex, Arc};
use std::sync::atomic::{AtomicBool, Ordering};
use std::thread;
use std::time::Duration;
use serialport::{SerialPort, available_ports};
use tauri::{AppHandle, Emitter, State};
use std::io::Write;

struct AppState {
    port: Mutex<Option<Box<dyn SerialPort>>>,
    port_name: Mutex<Option<String>>,
    connected: Arc<AtomicBool>,
}

#[tauri::command]
fn get_ports() -> Vec<String> {
    match available_ports() {
        Ok(ports) => ports.into_iter().map(|p| p.port_name).collect(),
        Err(_) => Vec::new(),
    }
}

fn disconnect_port_internal(state: &AppState) -> Result<(), String> {
    state.connected.store(false, Ordering::Relaxed);
    
    // Sleep a tiny bit to let the reader thread exit before dropping the port
    thread::sleep(Duration::from_millis(50));

    let mut state_port = state.port.lock().unwrap();
    let mut state_name = state.port_name.lock().unwrap();
    *state_port = None;
    *state_name = None;
    Ok(())
}

#[tauri::command]
fn connect_port(
    app_handle: AppHandle,
    state: State<'_, AppState>,
    port_name: String,
    baud_rate: u32,
) -> Result<String, String> {
    // Disconnect existing if any
    let _ = disconnect_port_internal(&state);

    // Try to open the port
    let port = serialport::new(&port_name, baud_rate)
        .timeout(Duration::from_millis(500))
        .open()
        .map_err(|e| format!("Failed to open port {}: {}", port_name, e))?;

    // Try to clone for reading
    let reader_port = port.try_clone().map_err(|e| format!("Failed to clone port: {}", e))?;

    // Save port and name in state
    let mut state_port = state.port.lock().unwrap();
    let mut state_name = state.port_name.lock().unwrap();
    *state_port = Some(port);
    *state_name = Some(port_name.clone());
    state.connected.store(true, Ordering::Relaxed);

    // Spawn reader thread
    let connected = state.connected.clone();
    thread::spawn(move || {
        use std::io::BufRead;
        let mut reader = std::io::BufReader::new(reader_port);
        loop {
            if !connected.load(Ordering::Relaxed) {
                break;
            }
            let mut line = String::new();
            match reader.read_line(&mut line) {
                Ok(0) => break, // EOF
                Ok(_) => {
                    let line = line.trim();
                    if line.starts_with("RPM:") {
                        let parts: Vec<&str> = line["RPM:".len()..].split(',').collect();
                        if parts.len() == 2 {
                            if let (Ok(target), Ok(actual)) = (parts[0].parse::<f32>(), parts[1].parse::<f32>()) {
                                #[derive(Clone, serde::Serialize)]
                                struct TelemetryPayload {
                                    target: f32,
                                    actual: f32,
                                }
                                let _ = app_handle.emit("telemetry", TelemetryPayload { target, actual });
                            }
                        }
                    } else if line.starts_with("PID:") {
                        let parts: Vec<&str> = line["PID:".len()..].split(',').collect();
                        if parts.len() == 3 {
                            if let (Ok(kp), Ok(ki), Ok(kd)) = (parts[0].parse::<f32>(), parts[1].parse::<f32>(), parts[2].parse::<f32>()) {
                                #[derive(Clone, serde::Serialize)]
                                struct PidPayload {
                                    kp: f32,
                                    ki: f32,
                                    kd: f32,
                                }
                                let _ = app_handle.emit("pid-params", PidPayload { kp, ki, kd });
                            }
                        }
                    }
                }
                Err(ref e) if e.kind() == std::io::ErrorKind::TimedOut => {
                    continue;
                }
                Err(_) => {
                    let _ = app_handle.emit("serial-status", "disconnected");
                    break;
                }
            }
        }
        connected.store(false, Ordering::Relaxed);
    });

    Ok(format!("Connected to {}", port_name))
}

#[tauri::command]
fn disconnect_port(state: State<'_, AppState>) -> Result<String, String> {
    disconnect_port_internal(&state)?;
    Ok("Disconnected".to_string())
}

#[tauri::command]
fn send_command(state: State<'_, AppState>, cmd: String) -> Result<(), String> {
    let mut port_guard = state.port.lock().unwrap();
    if let Some(ref mut port) = *port_guard {
        let mut full_cmd = cmd.clone();
        if !full_cmd.ends_with('\n') {
            full_cmd.push('\n');
        }
        port.write_all(full_cmd.as_bytes())
            .map_err(|e| format!("Failed to write to port: {}", e))?;
        port.flush()
            .map_err(|e| format!("Failed to flush port: {}", e))?;
        Ok(())
    } else {
        Err("Not connected".to_string())
    }
}

#[tauri::command]
fn get_connection_status(state: State<'_, AppState>) -> Result<Option<String>, String> {
    let connected = state.connected.load(Ordering::Relaxed);
    if connected {
        let name_guard = state.port_name.lock().unwrap();
        Ok(name_guard.clone())
    } else {
        Ok(None)
    }
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .manage(AppState {
            port: Mutex::new(None),
            port_name: Mutex::new(None),
            connected: Arc::new(AtomicBool::new(false)),
        })
        .invoke_handler(tauri::generate_handler![
            get_ports,
            connect_port,
            disconnect_port,
            send_command,
            get_connection_status
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}

