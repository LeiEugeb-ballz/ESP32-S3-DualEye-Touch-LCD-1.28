"use strict";
const byId = id => document.getElementById(id);
let admin = "", device = null, provider = null, stateLiveReady = false, working = false;
for (const id of ["device-ip", "gateway-ip"]) {
  byId(id).value = localStorage.getItem(`japa.${id}`) || "";
  byId(id).addEventListener("change", () => { localStorage.setItem(`japa.${id}`, byId(id).value.trim()); device = null; buttons(); });
}
function report(text, error = false) { byId("message").textContent = text; byId("message").classList.toggle("error", error); }
function rows(id, values) {
  const parent = byId(id); parent.replaceChildren();
  for (const [key, value] of Object.entries(values)) {
    const dt = document.createElement("dt"), dd = document.createElement("dd");
    dt.textContent = key; dd.textContent = String(value ?? "Unavailable"); parent.append(dt, dd);
  }
}
async function api(path, data, timeout = 8000) {
  const controller = new AbortController(), timer = setTimeout(() => controller.abort(), timeout);
  try {
    const response = await fetch(path, {signal: controller.signal, cache:"no-store", ...(data ? {method:"POST", headers:{"Content-Type":"application/json", "X-JAPA-Admin":admin}, body:JSON.stringify(data)} : {})});
    const text = await response.text(); let result;
    try { result = JSON.parse(text); } catch { result = {error:text}; }
    if (!response.ok) throw new Error(result.error || `Request failed (${response.status}): ${text}`);
    return result;
  } catch (error) { if (error.name === "AbortError") throw new Error("Request timed out. Check device power, Wi-Fi/IP and gateway availability."); throw error; }
  finally { clearTimeout(timer); }
}
function buttons() {
  byId("mistral").disabled = working || !provider?.ready || !device?.voice?.can_switch || !!device?.voice?.pending;
  byId("live").disabled = working || !stateLiveReady || !device?.voice?.can_switch || !!device?.voice?.pending;
  byId("xiaozhi").disabled = working || !device?.voice?.can_switch || !!device?.voice?.pending;
  byId("verify").disabled = working;
  byId("apply-persona").disabled = working;
}
async function refresh() {
  const state = await api("/api/status"); admin = state.admin_token; provider = state.provider; stateLiveReady = !!state.live?.ready;
  rows("provider-status", {"Mistral":provider.ready ? "Verified" : "Not verified", "Mistral issue":provider.error || "None", "GPT-Live":state.live.ready ? "Configured — session check required" : "Not configured", "Live issue":state.live.error || "None", "Live backend":`${state.live.model} → ${state.live.backend}`, "Persona":state.persona});
  byId("session-status").textContent = state.sessions.length ? JSON.stringify(state.sessions, null, 2) : "No Mistral session connected. Xiaozhi cloud sessions are not visible to this gateway.";
  buttons();
}
async function readDevice() {
  device = null; buttons();
  device = await api(`/api/device?ip=${encodeURIComponent(byId("device-ip").value.trim())}`);
  const b = device.boot, v = device.voice;
  rows("device-status", {"State":v.device_state, "Active provider":v.active, "Saved provider":v.selected, "Pending":v.pending || "None", "Last switch error":v.last_error || "None", "Running slot":b.running_label, "Internal free / min":`${b.internal_free} / ${b.internal_minimum} bytes`, "Largest internal block":b.internal_largest, "Allocation failures":b.allocation_failures, "Running ELF SHA256":b.elf_sha256}); buttons();
}
async function action(fn) {
  working = true; buttons();
  try { await fn(); } catch (error) { report(error.message, true); }
  finally { working = false; await refresh().catch(error => report(error.message, true)); buttons(); }
}
byId("connect").onclick = () => action(async () => { await readDevice(); report("Live device status received. No settings changed."); });
byId("verify").onclick = () => action(async () => { report("Running a small real Mistral speech/transcription/chat probe…"); await api("/api/readiness", {}, 65000); report("The complete Mistral voice chain passed its probe."); });
byId("live-verify").onclick = () => action(async () => { report("Checking GPT-Live configuration without opening a voice session…"); await api("/api/live/readiness", {}, 10000); report("GPT-Live configuration is present; the first device session will perform the authoritative startup check."); });
for (const [id, profile] of [["mistral", "mistral"], ["live", "live"], ["xiaozhi", "xiaozhi"]]) byId(id).onclick = () => action(async () => {
  await api("/api/provider", {ip:byId("device-ip").value.trim(), gateway_ip:byId("gateway-ip").value.trim(), profile}, 65000);
  report(profile === "live" ? "GPT-Live is selected on the host; the device profile was not rewritten." : "Selection requested. The device validates it before applying; read Active provider to confirm."); await readDevice();
});
byId("apply-persona").onclick = () => action(async () => { await api("/api/persona", {persona:byId("persona").value}); report("Mistral personality applied; its session history was cleared. Xiaozhi was not changed."); });
refresh().catch(error => report(error.message, true));
setInterval(() => { if (!working) { refresh().catch(error => report(error.message, true)); if (device) readDevice().catch(error => report(error.message, true)); } }, 5000);
