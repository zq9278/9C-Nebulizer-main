from __future__ import annotations

from collections import deque

from PyQt6.QtCore import QPointF, QRectF
from PyQt6.QtGui import QColor, QPainter, QPainterPath, QPen
from PyQt6.QtWidgets import QWidget


class TrendWidget(QWidget):
    def __init__(self) -> None:
        super().__init__()
        self._samples: deque[dict[str, float]] = deque(maxlen=180)
        self._pid_summary = "PID: N/A"
        self._visible = {
            "outlet": True,
            "kettle": True,
            "target": True,
            "output": True,
            "error": True,
            "integral": True,
        }
        self.setMinimumHeight(320)

    def add_sample(
        self,
        outlet_c: float,
        kettle_c: float,
        target_c: float,
        output_percent: float,
        error_c: float,
        integral_percent: float,
    ) -> None:
        self._samples.append(
            {
                "outlet": outlet_c,
                "kettle": kettle_c,
                "target": target_c,
                "output": output_percent,
                "error": error_c,
                "integral": integral_percent,
            }
        )
        self.update()

    def clear_samples(self) -> None:
        self._samples.clear()
        self._pid_summary = "PID: N/A"
        self.update()

    def set_pid_summary(self, kp: float, ki: float, kd: float, i_limit: int) -> None:
        self._pid_summary = f"PID  Kp={kp:.3f}  Ki={ki:.3f}  Kd={kd:.3f}  I={i_limit}"
        self.update()

    def set_series_visible(self, name: str, visible: bool) -> None:
        if name in self._visible:
            self._visible[name] = visible
            self.update()

    def paintEvent(self, event) -> None:  # type: ignore[override]
        painter = QPainter(self)
        painter.fillRect(self.rect(), QColor("#11161d"))
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)

        outer = self.rect().adjusted(44, 16, -14, -28)
        top_rect = QRectF(outer.left(), outer.top() + 20, outer.width(), outer.height() * 0.54)
        bottom_rect = QRectF(
            outer.left(),
            top_rect.bottom() + 28,
            outer.width(),
            outer.bottom() - top_rect.bottom() - 28,
        )

        painter.setPen(QColor("#d8dee9"))
        painter.drawText(int(outer.left()), int(outer.top()), self._pid_summary)

        self._draw_grid(painter, top_rect)
        self._draw_grid(painter, bottom_rect)

        self._draw_axis_labels(
            painter,
            top_rect,
            ("60C", "50C", "40C", "30C", "20C"),
        )
        self._draw_axis_labels(
            painter,
            bottom_rect,
            ("100%", "75", "50", "25", "-10C"),
        )

        self._draw_series(painter, top_rect, "outlet", 20.0, 60.0, QColor("#ff6b6b"))
        self._draw_series(painter, top_rect, "kettle", 20.0, 60.0, QColor("#4ecdc4"))
        self._draw_series(painter, top_rect, "target", 20.0, 60.0, QColor("#ffe66d"))
        self._draw_series(painter, bottom_rect, "output", 0.0, 100.0, QColor("#7bd389"))
        self._draw_series(painter, bottom_rect, "error", -10.0, 20.0, QColor("#a78bfa"))
        self._draw_series(painter, bottom_rect, "integral", 0.0, 100.0, QColor("#f59e0b"))

        painter.setPen(QColor("#ff6b6b"))
        painter.drawText(int(outer.left()), self.height() - 8, "Outlet")
        painter.setPen(QColor("#4ecdc4"))
        painter.drawText(int(outer.left()) + 70, self.height() - 8, "Kettle")
        painter.setPen(QColor("#ffe66d"))
        painter.drawText(int(outer.left()) + 140, self.height() - 8, "Target")
        painter.setPen(QColor("#7bd389"))
        painter.drawText(int(outer.left()) + 210, self.height() - 8, "Power")
        painter.setPen(QColor("#a78bfa"))
        painter.drawText(int(outer.left()) + 275, self.height() - 8, "Error")
        painter.setPen(QColor("#f59e0b"))
        painter.drawText(int(outer.left()) + 335, self.height() - 8, "Integral")

    def _draw_grid(self, painter: QPainter, rect: QRectF) -> None:
        painter.setPen(QColor("#293241"))
        for i in range(5):
            y = rect.top() + rect.height() * i / 4
            painter.drawLine(int(rect.left()), int(y), int(rect.right()), int(y))

    def _draw_axis_labels(self, painter: QPainter, rect: QRectF, labels: tuple[str, ...]) -> None:
        painter.setPen(QColor("#d8dee9"))
        for i, label in enumerate(labels):
            y = rect.top() + rect.height() * i / max(1, len(labels) - 1)
            painter.drawText(8, int(y) + 4, label)

    def _draw_series(
        self,
        painter: QPainter,
        rect: QRectF,
        name: str,
        min_y: float,
        max_y: float,
        color: QColor,
    ) -> None:
        if (not self._visible.get(name, True)) or (len(self._samples) < 2):
            return

        path = QPainterPath()
        sample_count = len(self._samples)
        for i, sample in enumerate(self._samples):
            value = sample[name]
            normalized = (value - min_y) / (max_y - min_y)
            normalized = max(0.0, min(1.0, normalized))
            x = rect.left() + rect.width() * i / max(1, sample_count - 1)
            y = rect.bottom() - normalized * rect.height()
            point = QPointF(x, y)
            if i == 0:
                path.moveTo(point)
            else:
                path.lineTo(point)

        painter.setPen(QPen(color, 2.0))
        painter.drawPath(path)
