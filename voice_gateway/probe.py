"""Explicit small live-provider acceptance probe; never logs credentials or inputs."""
import asyncio
import json
from .provider import MistralProvider


async def run():
    provider = MistralProvider()
    try:
        result = await provider.verify()
        print(json.dumps(provider.status()))
        return result
    finally:
        await provider.close()


if __name__ == "__main__":
    raise SystemExit(0 if asyncio.run(run()) else 1)
