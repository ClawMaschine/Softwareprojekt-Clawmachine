import time
from contextlib import asynccontextmanager

import uvicorn
from fastapi import FastAPI, Response
from fastapi.responses import StreamingResponse

try:
    from python_server.camera import Camera
    from python_server.configuration_loader import load_camera_configuration
except ModuleNotFoundError:
    from camera import Camera
    from configuration_loader import load_camera_configuration


# Multipart-Trenner für den MJPEG-Stream — beliebiger, aber eindeutiger String,
# muss im Content-Type-Header (boundary=...) und vor jedem Frame identisch sein.
MJPEG_STREAM_BOUNDARY = "frame"

camera_configuration = load_camera_configuration()
camera = Camera(
    device_index=camera_configuration.device_index,
    jpeg_quality=camera_configuration.jpeg_quality,
)


@asynccontextmanager
async def lifespan(_app: FastAPI):
    # Kamera fehlt/ist nicht angeschlossen -> Server trotzdem hochfahren lassen
    # (z.B. damit andere zukünftige HTTP-Endpunkte weiter funktionieren), nur
    # /camera/stream liefert dann einen Fehler statt Bildern.
    try:
        camera.open()
    except RuntimeError as error:
        print(f"[WEBSERVER] {error}")
    yield
    camera.close()


app = FastAPI(lifespan=lifespan)


def generate_mjpeg_frames():
    frame_interval_seconds = 1 / camera_configuration.frame_rate
    while True:
        jpeg_bytes = camera.read_jpeg_frame()
        if jpeg_bytes is not None:
            yield (
                b"--" + MJPEG_STREAM_BOUNDARY.encode() + b"\r\n"
                b"Content-Type: image/jpeg\r\n"
                b"Content-Length: " + str(len(jpeg_bytes)).encode() + b"\r\n\r\n"
                + jpeg_bytes + b"\r\n"
            )
        time.sleep(frame_interval_seconds)


@app.get("/camera/stream")
def camera_stream():
    if not camera.is_open():
        return {"error": "Kamera ist nicht verfügbar — siehe Server-Log."}

    return StreamingResponse(
        generate_mjpeg_frames(),
        media_type=f"multipart/x-mixed-replace; boundary={MJPEG_STREAM_BOUNDARY}",
    )


# Einzelbild statt Dauer-Stream — für Browser, die multipart/x-mixed-replace
# im <img> nicht unterstützen (z.B. der Captive-Portal-Browser der Switch 2).
# second_page/camera-frame.html fragt das per <meta http-equiv="refresh">
# alle paar Sekunden neu ab, das ergibt einen "Diashow"-artigen Live-Eindruck
# ganz ohne JavaScript.
@app.get("/camera/snapshot")
def camera_snapshot():
    if not camera.is_open():
        return Response(status_code=503)

    jpeg_bytes = camera.read_jpeg_frame()
    if jpeg_bytes is None:
        return Response(status_code=503)

    return Response(
        content=jpeg_bytes,
        media_type="image/jpeg",
        # Ohne das würde ein Browser das Bild bei jedem Refresh aus dem Cache
        # zeigen statt ein neues abzuholen — dann bliebe es beim ersten Frame stehen.
        headers={"Cache-Control": "no-store, no-cache, must-revalidate"},
    )


def run_webserver():
    uvicorn.run(app, host="0.0.0.0", port=camera_configuration.http_port)
