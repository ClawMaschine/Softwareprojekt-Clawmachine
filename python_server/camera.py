import cv2


class Camera:
    """Kapselt eine USB-Webcam via OpenCV/V4L2 (/dev/video<device_index>)."""

    def __init__(self, device_index: int, jpeg_quality: int):
        self.device_index = device_index
        self.jpeg_quality = jpeg_quality
        self.video_capture = None

    def open(self):
        self.video_capture = cv2.VideoCapture(self.device_index)
        if not self.video_capture.isOpened():
            self.video_capture = None
            raise RuntimeError(
                f"Kamera an /dev/video{self.device_index} konnte nicht geöffnet werden "
                "(angeschlossen? richtiger device_index in config.ini?)"
            )

    def is_open(self) -> bool:
        return self.video_capture is not None

    def read_jpeg_frame(self) -> bytes | None:
        if self.video_capture is None:
            return None

        is_frame_read, frame = self.video_capture.read()
        if not is_frame_read:
            return None

        is_frame_encoded, jpeg_buffer = cv2.imencode(
            ".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, self.jpeg_quality]
        )
        if not is_frame_encoded:
            return None

        return jpeg_buffer.tobytes()

    def close(self):
        if self.video_capture is not None:
            self.video_capture.release()
            self.video_capture = None
