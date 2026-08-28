import asyncio
import json
import os
import websockets

# Configuration
XIAOZHI_MCP_WS_URL = "wss://api.xiaozhi.me/mcp/?token=eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCJ9.eyJ1c2VySWQiOjEwNTc1MDMsImFnZW50SWQiOjIyODI2NjUsImVuZHBvaW50SWQiOiJhZ2VudF8yMjgyNjY1IiwicHVycG9zZSI6Im1jcC1lbmRwb2ludCIsImlhdCI6MTc4NzkzOTAyNSwiZXhwIjoxODE5NDk2NjI1fQ.EXEWuRSGPD3QX8A1CQGjXqAeCmuZzMDqRvZdkQRA-7L2QUYdDsRSfjLhRsch5I0NP3Zm3JWaTQRwDY2gsu6qaw"
SHARED_DIRECTORY = r"C:\Japa_Shared"

os.makedirs(SHARED_DIRECTORY, exist_ok=True)

def safe_resolve(filename: str) -> str:
    target = os.path.abspath(os.path.join(SHARED_DIRECTORY, filename))
    if not target.startswith(os.path.abspath(SHARED_DIRECTORY)):
        raise PermissionError("Access outside shared directory is blocked.")
    return target

# Tool Handlers
def handle_list_files(args):
    files = os.listdir(SHARED_DIRECTORY)
    return {"status": "success", "files": files}

def handle_read_file(args):
    filename = args.get("filename", "")
    target = safe_resolve(filename)
    if not os.path.exists(target):
        return {"status": "error", "message": f"File '{filename}' not found."}
    with open(target, "r", encoding="utf-8") as f:
        return {"status": "success", "content": f.read()}

def handle_write_file(args):
    filename = args.get("filename", "")
    content = args.get("content", "")
    target = safe_resolve(filename)
    with open(target, "w", encoding="utf-8") as f:
        f.write(content)
    return {"status": "success", "message": f"Wrote {len(content)} bytes to {filename}"}

TOOLS = [
    {
        "name": "list_files",
        "description": "Lists all available files in Bladder's shared workspace directory.",
        "inputSchema": {
            "type": "object",
            "properties": {}
        }
    },
    {
        "name": "read_file",
        "description": "Reads the entire text content of a file from Bladder's workspace.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "filename": {"type": "string", "description": "The relative name of the file to read"}
            },
            "required": ["filename"]
        }
    },
    {
        "name": "write_file",
        "description": "Writes or overwrites text content to a file in Bladder's workspace.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "filename": {"type": "string", "description": "The relative name of the file to create/overwrite"},
                "content": {"type": "string", "description": "The exact full text content to write"}
            },
            "required": ["filename", "content"]
        }
    }
]

TOOL_MAP = {
    "list_files": handle_list_files,
    "read_file": handle_read_file,
    "write_file": handle_write_file,
}

async def run_mcp_bridge():
    while True:
        try:
            print(f"[Connecting] Connecting to XiaoZhi Access Point...")
            async with websockets.connect(XIAOZHI_MCP_WS_URL) as ws:
                print(f"[Connected] Bridge online. Syncing tools schema...")
                
                # Initial handshake / tool registration
                init_msg = {
                    "type": "register_tools",
                    "tools": TOOLS
                }
                await ws.send(json.dumps(init_msg))

                async for message in ws:
                    data = json.loads(message)
                    msg_type = data.get("type")

                    if msg_type == "call_tool":
                        call_id = data.get("id")
                        tool_name = data.get("name")
                        tool_args = data.get("arguments", {})

                        print(f"[Execute] Tool: {tool_name} | Args: {tool_args}")
                        
                        handler = TOOL_MAP.get(tool_name)
                        if handler:
                            try:
                                result = handler(tool_args)
                            except Exception as err:
                                result = {"status": "error", "message": str(err)}
                        else:
                            result = {"status": "error", "message": f"Unknown tool: {tool_name}"}

                        # Send response back to cloud
                        response = {
                            "type": "tool_response",
                            "id": call_id,
                            "result": result
                        }
                        await ws.send(json.dumps(response))

        except Exception as e:
            print(f"[Disconnected] Connection dropped ({e}). Retrying in 5s...")
            await asyncio.sleep(5)

if __name__ == "__main__":
    asyncio.run(run_mcp_bridge())