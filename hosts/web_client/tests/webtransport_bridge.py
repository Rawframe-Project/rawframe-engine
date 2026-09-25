"""The network side of a stand-in for the browser's WebTransport, for the
page tests under Node (webtransport_node.mjs): aioquic, an independent HTTP/3
and WebTransport implementation, opens one session to the URL given and
trusts the server only if its certificate's SHA-256 is one of the hashes
given, as a browser's serverCertificateHashes does. It speaks JSON lines:
commands on standard input, events on standard output.

  commands  {"op": "bidi"|"uni", "id": n}  {"op": "write", "id": n, "data": b64}
            {"op": "datagram", "data": b64}  {"op": "close"}
  events    {"ev": "ready"}  {"ev": "closed", "error": bool}
            {"ev": "datagram", "data": b64}  {"ev": "data", "id": n, "data": b64}
            {"ev": "incoming", "peer": q, "uni": bool}  {"ev": "data", "peer": q, "data": b64}

usage: webtransport_bridge.py <url> <hash,hash,...>
"""
import asyncio
import base64
import hashlib
import json
import ssl
import sys
from urllib.parse import urlparse

from aioquic.asyncio.client import connect
from aioquic.asyncio.protocol import QuicConnectionProtocol
from aioquic.h3.connection import H3_ALPN, FrameType, H3Connection
from aioquic.h3.events import DatagramReceived, HeadersReceived, WebTransportStreamDataReceived
from aioquic.quic.configuration import QuicConfiguration
from aioquic.quic.events import ConnectionTerminated
from cryptography.hazmat.primitives.serialization import Encoding


def say(event):
    sys.stdout.write(json.dumps(event) + "\n")
    sys.stdout.flush()


class Page(QuicConnectionProtocol):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.http = H3Connection(self._quic, enable_webtransport=True)
        self.status = asyncio.get_event_loop().create_future()
        self.ended = asyncio.Event()
        self.ours = {}
        self.theirs = set()

    def quic_event_received(self, event):
        if isinstance(event, ConnectionTerminated):
            self.ended.set()
        for http_event in self.http.handle_event(event):
            if isinstance(http_event, HeadersReceived) and not self.status.done():
                self.status.set_result(dict(http_event.headers).get(b":status"))
            elif isinstance(http_event, DatagramReceived):
                say({"ev": "datagram", "data": base64.b64encode(http_event.data).decode()})
            elif isinstance(http_event, WebTransportStreamDataReceived):
                data = base64.b64encode(http_event.data).decode()
                if http_event.stream_id in self.ours:
                    say({"ev": "data", "id": self.ours[http_event.stream_id], "data": data})
                    continue
                if http_event.stream_id not in self.theirs:
                    self.theirs.add(http_event.stream_id)
                    say({"ev": "incoming", "peer": http_event.stream_id, "uni": http_event.stream_id % 4 == 3})
                if http_event.data:
                    say({"ev": "data", "peer": http_event.stream_id, "data": data})


async def main(url, hashes):
    where = urlparse(url)
    configuration = QuicConfiguration(is_client=True, alpn_protocols=H3_ALPN, max_datagram_frame_size=65536)
    # Trust by hash alone, checked below, as serverCertificateHashes does.
    configuration.verify_mode = ssl.CERT_NONE
    async with connect(where.hostname, where.port, configuration=configuration, create_protocol=Page) as page:
        certificate = page._quic.tls._peer_certificate.public_bytes(Encoding.DER)
        if hashlib.sha256(certificate).hexdigest() not in hashes:
            say({"ev": "closed", "error": True})
            return
        session = page._quic.get_next_available_stream_id()
        page.http.send_headers(
            stream_id=session,
            headers=[
                (b":method", b"CONNECT"),
                (b":protocol", b"webtransport"),
                (b":scheme", b"https"),
                (b":authority", where.netloc.encode()),
                (b":path", (where.path or "/").encode()),
            ],
        )
        page.transmit()
        if await asyncio.wait_for(page.status, 5) != b"200":
            say({"ev": "closed", "error": True})
            return
        say({"ev": "ready"})
        reader = asyncio.StreamReader()
        await asyncio.get_event_loop().connect_read_pipe(
            lambda: asyncio.StreamReaderProtocol(reader), sys.stdin)
        ended = asyncio.ensure_future(page.ended.wait())
        while True:
            line = asyncio.ensure_future(reader.readline())
            done, _ = await asyncio.wait({line, ended}, return_when=asyncio.FIRST_COMPLETED)
            if ended in done or not line.result():
                break
            command = json.loads(line.result())
            if command["op"] in ("bidi", "uni"):
                one_way = command["op"] == "uni"
                stream = page.http.create_webtransport_stream(session, is_unidirectional=one_way)
                if not one_way:
                    # aioquic 0.9 does not mark its own two-way stream as the
                    # session's; a browser reads the answers as its bytes.
                    opened = page.http._get_or_create_stream(stream)
                    opened.frame_type = FrameType.WEBTRANSPORT_STREAM
                    opened.session_id = session
                page.ours[stream] = command["id"]
                page.ours_by_id = getattr(page, "ours_by_id", {})
                page.ours_by_id[command["id"]] = stream
            elif command["op"] == "write":
                page._quic.send_stream_data(page.ours_by_id[command["id"]], base64.b64decode(command["data"]))
            elif command["op"] == "datagram":
                page.http.send_datagram(session, base64.b64decode(command["data"]))
            elif command["op"] == "close":
                break
            page.transmit()
        page._quic.close()
        page.transmit()
    say({"ev": "closed", "error": False})


if __name__ == "__main__":
    asyncio.run(main(sys.argv[1], sys.argv[2].split(",")))
