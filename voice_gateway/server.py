"""LAN Opus gateway with loopback-only administration and redacted live status."""
import argparse
import asyncio
import hmac
import ipaddress
import json
import os
from pathlib import Path
import secrets
from collections import deque
import time
from aiohttp import web, ClientSession, ClientTimeout, WSMsgType
from .provider import MistralProvider
from .session import VoiceSession, PERSONAS, set_event_logger
from .live_provider import LiveProvider, LiveVoiceSession

ROOT = Path(__file__).resolve().parent

EVENT_LOG = deque(maxlen=100)

def push_event(source, message, level="info"):
    entry = {
        "timestamp": time.time(),
        "time": time.strftime("%H:%M:%S"),
        "source": source,
        "message": message,
        "level": level
    }
    EVENT_LOG.append(entry)

set_event_logger(push_event)
push_event("GATEWAY", "JAPA voice gateway initialized", "success")



def private_device(value):
    try:
        address = ipaddress.IPv4Address(value)
    except (ValueError, TypeError):
        raise web.HTTPBadRequest(text="Enter the device's LAN IPv4 address, without a port.")
    if not address.is_private or address.is_loopback or address.is_unspecified or address.is_multicast or int(address) == 0xffffffff:
        raise web.HTTPBadRequest(text="A private device IPv4 address is required.")
    return str(address)


@web.middleware
async def admin_boundary(request, handler):
    if request.method == "OPTIONS":
        resp = web.Response(status=204)
        resp.headers["Access-Control-Allow-Origin"] = "*"
        resp.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS"
        resp.headers["Access-Control-Allow-Headers"] = "Content-Type, X-JAPA-Admin, X-JAPA-Control, Authorization"
        return resp

    if request.path != "/xiaozhi/v1":
        try:
            local = ipaddress.ip_address(request.remote or "0.0.0.0").is_loopback
        except ValueError:
            local = False
        # Bind to LAN for the device, but never expose administration/token to LAN peers.
        if not local or request.host not in ("localhost:8765", "127.0.0.1:8765"):
            raise web.HTTPForbidden(text="Open the control board on this PC at http://localhost:8765")
        if request.method == "POST" and request.headers.get("Origin", "") not in ("", "null", "http://localhost:8765", "http://127.0.0.1:8765"):
            raise web.HTTPForbidden(text="Origin rejected")
        if request.method == "POST" and not hmac.compare_digest(request.headers.get("X-JAPA-Admin", ""), request.app["admin_token"]):
            raise web.HTTPForbidden(text="Refresh the control board before changing settings")
    try:
        response = await handler(request)
    except (asyncio.TimeoutError, OSError) as error:
        response = web.json_response({"error": f"Device unavailable ({type(error).__name__}). Check power, Wi-Fi/IP and port 3233."}, status=502)
    response.headers["Cache-Control"] = "no-store"
    response.headers["X-Content-Type-Options"] = "nosniff"
    response.headers["Access-Control-Allow-Origin"] = "*"
    response.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS"
    response.headers["Access-Control-Allow-Headers"] = "Content-Type, X-JAPA-Admin, X-JAPA-Control, Authorization"
    return response


async def status(request):
    app = request.app
    return web.json_response({"service": "JAPA voice gateway", "provider": app["provider"].status(),
        "live": app["live"].status(),
        "selected_provider": "live" if app["live_state"]["preferred"] else "mistral",
        "persona": app["persona"], "personas": list(PERSONAS), "admin_token": app["admin_token"],
        "sessions": [{"state": s.state, "persona": getattr(s, "persona", "live"), "metrics": s.metrics} for s in app["sessions"] if s is not None],
        "events": list(EVENT_LOG),
        "limitations": ["No acoustic barge-in: current firmware AEC is disabled.",
                         "Host PC must remain running for Mistral.", "Human naturalness acceptance is outstanding."]})


async def get_events(request):
    try:
        since = float(request.query.get("since", "0"))
    except (ValueError, TypeError):
        since = 0.0
    recent = [e for e in EVENT_LOG if e["timestamp"] > since]
    latest = EVENT_LOG[-1]["timestamp"] if EVENT_LOG else 0.0
    return web.json_response({"events": recent, "latest": latest})



async def readiness(request):
    ok = await request.app["provider"].verify()
    return web.json_response(request.app["provider"].status(), status=200 if ok else 503)


async def live_readiness(request):
    ok = await request.app["live"].verify()
    return web.json_response(request.app["live"].status(), status=200 if ok else 503)


async def personality(request):
    data = await request.json()
    persona = data.get("persona")
    if persona not in PERSONAS:
        raise web.HTTPBadRequest(text="Unknown persona")
    if any(session.busy for session in request.app["sessions"]):
        raise web.HTTPConflict(text="Finish listening/speaking before changing personality")
    request.app["persona"] = persona
    for session in request.app["sessions"]:
        session.persona = persona
        session.history.clear()
    return web.json_response({"persona": persona, "history": "cleared; not transferred"})


async def device_request(app, ip, method, path, body=None, token=None):
    headers = {"X-JAPA-Control": token} if token else {}
    async with app["http"].request(method, f"http://{ip}:3233{path}", json=body, headers=headers, allow_redirects=False) as response:
        if response.content_length and response.content_length > 16384:
            raise web.HTTPBadGateway(text="Unexpected device response size")
        raw = bytearray()
        async for chunk in response.content.iter_chunked(2048):
            raw.extend(chunk)
            if len(raw) > 16384:
                break
        if len(raw) > 16384:
            raise web.HTTPBadGateway(text="Unexpected device response size")
        try:
            data = json.loads(raw)
        except (ValueError, UnicodeError):
            raise web.HTTPBadGateway(text="Not a JAPA JSON endpoint. Check device IP and firmware.")
        if response.status >= 300:
            raise web.HTTPBadGateway(text=str(data.get("error", "Device request rejected")))
        return data


async def device_status(request):
    ip = private_device(request.query.get("ip"))
    boot = await device_request(request.app, ip, "GET", "/boot/status")
    voice = await device_request(request.app, ip, "GET", "/voice/status")
    if boot.get("service") != "dualeye_boot_control" or voice.get("service") != "japa_voice_control":
        raise web.HTTPBadGateway(text="Device needs the matching JAPA control firmware")
    voice.pop("control_token", None)
    return web.json_response({"ip": ip, "boot": boot, "voice": voice})


async def select_provider(request):
    data = await request.json()
    ip = private_device(data.get("ip"))
    target = data.get("profile")
    if target not in ("xiaozhi", "gateway", "mistral", "live"):
        raise web.HTTPBadRequest(text="Unknown voice provider")
    voice = await device_request(request.app, ip, "GET", "/voice/status")
    if voice.get("service") != "japa_voice_control" or not voice.get("control_token"):
        raise web.HTTPBadGateway(text="Device does not support safe profile switching")
    if not voice.get("can_switch"):
        raise web.HTTPConflict(text="Finish the conversation first; no settings were changed")
    if target == "live":
        if not await request.app["live"].verify():
            return web.json_response({"error": "GPT-Live configuration is unavailable; device profile unchanged",
                                      "live": request.app["live"].status()}, status=503)
        if voice.get("selected") not in ("gateway", "mistral"):
            raise web.HTTPConflict(text="Select Mistral gateway first; Live uses the same gateway profile")
        request.app["live_state"]["preferred"] = True
        return web.json_response({"selected_provider": "live", "device_profile": voice.get("selected"),
                                  "applied": "host_preference_only"})
    if target == "mistral":
        request.app["live_state"]["preferred"] = False
        target = "gateway"
    payload = {"profile": target}
    if target == "gateway":
        if not await request.app["provider"].verify():
            return web.json_response({"error": "Mistral readiness failed; device profile unchanged", "provider": request.app["provider"].status()}, status=503)
        gateway_ip = private_device(data.get("gateway_ip"))
        payload.update(url=f"ws://{gateway_ip}:8765/xiaozhi/v1", token=request.app["pairing_token"])
    result = await device_request(request.app, ip, "POST", "/voice/profile", payload, voice["control_token"])
    return web.json_response(result, status=202)


async def websocket(request):
    app = request.app
    if not hmac.compare_digest(request.headers.get("Authorization", ""), "Bearer " + app["pairing_token"]):
        raise web.HTTPUnauthorized(text="Device pairing token required")
    if request.headers.get("Protocol-Version") != "1":
        raise web.HTTPBadRequest(text="Opus protocol version 1 required")
    if not app["provider"].status()["ready"] and not app["live"].ready:
        raise web.HTTPServiceUnavailable(text="Validate a voice provider on the local control board first")
    if len(app["sessions"]) >= 2:
        raise web.HTTPServiceUnavailable(text="Gateway session limit reached")
    ws = web.WebSocketResponse(max_msg_size=8192, heartbeat=25, receive_timeout=180)
    await ws.prepare(request)
    session = None
    try:
        hello = await asyncio.wait_for(ws.receive_json(), 5)
        audio = hello.get("audio_params", {})
        if hello.get("type") != "hello" or hello.get("version") != 1 or audio != {
                "format": "opus", "sample_rate": 16000, "channels": 1, "frame_duration": 60}:
            await ws.close(code=1003, message=b"Expected_16k_mono_60ms_Opus")
            return ws
        features = hello.get("features") or {}
        use_live = bool(features.get("japa_live")) and app["live_state"]["preferred"] and app["live"].ready
        if bool(features.get("japa_live")) and not app["live"].ready:
            await ws.close(code=1011, message=b"GPT-Live_not_configured")
            return ws
        if use_live:
            session = LiveVoiceSession(ws, app["live"], app["http"])
        else:
            session = VoiceSession(ws, app["provider"], app["persona"])
        if not use_live and not app["provider"].status()["ready"]:
            await ws.close(code=1011, message=b"Mistral_not_verified")
            return ws
        app["sessions"].add(session)
        push_event("ESP32", "Hardware connected (Opus 16kHz audio session opened)", "success")
        hello_fields = {"version": 1, "transport": "websocket", "japa_ready": True,
                        "japa_live": use_live, "audio_params": audio}
        if use_live:
            await session.send_device("hello", **hello_fields)
            await session.start()
        else:
            await session.send("hello", **hello_fields)
            asyncio.create_task(session.trigger_startup_greeting())
        async for message in ws:
            if message.type == WSMsgType.BINARY:
                if use_live:
                    await session.send_audio(message.data)
                else:
                    session.audio(message.data)
            elif message.type == WSMsgType.TEXT:
                payload = json.loads(message.data)
                if not isinstance(payload, dict):
                    raise ValueError("object_required")
                if use_live:
                    if payload.get("type") == "abort":
                        session.metrics["interruptions"] += 1
                    # Live owns turn detection; preserve control messages only
                    # for explicit close/abort at the gateway boundary.
                    if payload.get("type") == "abort":
                        await session.close()
                else:
                    await session.control(payload)
            elif message.type == WSMsgType.ERROR:
                break
    except Exception:
        await ws.close(code=1003, message=b"Invalid_or_timed_out_protocol")
    finally:
        if session is not None:
            await session.close()
            app["sessions"].discard(session)
            push_event("ESP32", "Hardware session closed", "warn")
        return ws


async def lifecycle(app):
    async with ClientSession(timeout=ClientTimeout(total=5), trust_env=False) as http:
        app["http"] = http
        yield
        for session in list(app["sessions"]):
            if session is not None:
                await session.ws.close(code=1001, message=b"Gateway_stopping")
                await session.close()
        await app["provider"].close()


def make_app(provider=None, pairing_token=None):
    app = web.Application(middlewares=[admin_boundary], client_max_size=2048)
    app.update(provider=provider or MistralProvider(), live=LiveProvider(), live_state={"preferred": False},
               pairing_token=pairing_token or secrets.token_urlsafe(32),
               admin_token=secrets.token_urlsafe(32), persona="japa", sessions=set())
    app.cleanup_ctx.append(lifecycle)
    async def page(request):
        name = request.path.lstrip("/") or "index.html"
        unified = ROOT.parent / "xiaozhi-esp32" / "web_ui" / name
        if unified.exists() and unified.is_file():
            return web.FileResponse(unified)
        return web.FileResponse(ROOT / "ui" / name)
    app.router.add_get("/", page)
    app.router.add_get("/control.js", page)
    app.router.add_get("/style.css", page)
    app.router.add_get("/api/status", status)
    app.router.add_get("/api/events", get_events)
    app.router.add_post("/api/readiness", readiness)
    app.router.add_post("/api/live/readiness", live_readiness)
    app.router.add_post("/api/persona", personality)
    app.router.add_get("/api/device", device_status)
    app.router.add_post("/api/provider", select_provider)
    app.router.add_get("/xiaozhi/v1", websocket)
    return app


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="0.0.0.0")
    args = parser.parse_args()
    token = os.environ.get("JAPA_GATEWAY_TOKEN", "")
    if not token:
        path = ROOT / ".pairing-token"
        if not path.exists():
            # Runtime pairing state, not an API key. Ignored by Git.
            path.write_text(secrets.token_urlsafe(32), encoding="ascii")
        token = path.read_text(encoding="ascii").strip()
    if len(token) < 16:
        raise SystemExit("JAPA_GATEWAY_TOKEN must contain at least 16 characters")
    web.run_app(make_app(pairing_token=token), host=args.bind, port=8765, access_log=None)


if __name__ == "__main__":
    main()
