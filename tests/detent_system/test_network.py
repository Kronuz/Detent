from __future__ import annotations

import asyncio
import unittest

from network import PeerLink


class PeerLinkTests(unittest.IsolatedAsyncioTestCase):
    async def test_partition_settles_tunnel_and_reconnects_after_heal(self) -> None:
        echoes: set[asyncio.Task[None]] = set()

        async def echo(
            reader: asyncio.StreamReader, writer: asyncio.StreamWriter
        ) -> None:
            try:
                while data := await reader.read(8192):
                    writer.write(data)
                    await writer.drain()
            finally:
                writer.close()
                await writer.wait_closed()

        def accepted(
            reader: asyncio.StreamReader, writer: asyncio.StreamWriter
        ) -> None:
            task = asyncio.create_task(echo(reader, writer))
            echoes.add(task)
            task.add_done_callback(echoes.discard)

        server = await asyncio.start_server(accepted, "127.0.0.1", 0)
        link = PeerLink(server.sockets[0].getsockname()[1])
        port = await link.start()
        writers: list[asyncio.StreamWriter] = []
        try:
            reader, writer = await asyncio.open_connection("127.0.0.1", port)
            writers.append(writer)
            writer.write(b"before")
            await writer.drain()
            self.assertEqual(
                await asyncio.wait_for(reader.readexactly(6), 2), b"before"
            )
            await link.block()
            self.assertEqual(await asyncio.wait_for(reader.read(), 2), b"")
            self.assertFalse(link.writers)
            link.blocked = False
            reader, writer = await asyncio.open_connection("127.0.0.1", port)
            writers.append(writer)
            writer.write(b"after")
            await writer.drain()
            self.assertEqual(await asyncio.wait_for(reader.readexactly(5), 2), b"after")
        finally:
            await link.close()
            for writer in writers:
                writer.close()
                await writer.wait_closed()
            server.close()
            await server.wait_closed()
            if echoes:
                await asyncio.gather(*tuple(echoes))
        self.assertFalse(link.tasks)
        self.assertFalse(link.writers)


if __name__ == "__main__":
    unittest.main()
