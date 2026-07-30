from __future__ import annotations

from collections import deque

from PyQt6.QtCore import QPointF, QRectF, Qt
from PyQt6.QtGui import QColor, QMouseEvent, QPainter, QPainterPath, QPen, QWheelEvent
from PyQt6.QtWidgets import QWidget


class TrendWidget(QWidget):
    def __init__(self) -> None:
        super().__init__()
        self._samples: deque[dict[str, float]] = deque(maxlen=3600)
        self._pid_summary = "PID: N/A"
        self._visible = {
            "outlet": True,
            "stage": True,
            "target": True,
            "output": True,
            "error": True,
            "integral": True,
        }
        self._view_span = self._samples.maxlen or 180
        self._view_offset = 0
        self._drag_last_x: float | None = None
        self._auto_follow = True
        self.setMinimumHeight(320)
        self.setMouseTracking(True)

    def add_sample(
        self,
        outlet_c: float,
        stage_c: float,
        target_c: float,
        output_percent: float,
        error_c: float,
        integral_percent: float,
    ) -> None:
        self._samples.append(
            {
                "outlet": outlet_c,
                "stage": stage_c,
                "target": target_c,
                "output": output_percent,
                "error": error_c,
                "integral": integral_percent,
            }
        )
        if self._auto_follow:
            self._view_offset = 0
        else:
            self._view_offset += 1
            self._clamp_view()
        self.update()

    def clear_samples(self) -> None:
        self._samples.clear()
        self._pid_summary = "PID: N/A"
        self._view_offset = 0
        self._auto_follow = True
        self.update()

    def set_pid_summary(self, kp: float, ki: float, kd: float, i_limit: int) -> None:
        self._pid_summary = f"PID  Kp={kp:.3f}  Ki={ki:.3f}  Kd={kd:.3f}  I={i_limit}"
        self.update()

    def set_series_visible(self, name: str, visible: bool) -> None:
        if name in self._visible:
            self._visible[name] = visible
            self.update()

    def zoom_in(self) -> None:
        self._zoom_view(0.75)

    def zoom_out(self) -> None:
        self._zoom_view(2.0)

    def reset_view(self) -> None:
        self._view_span = self._samples.maxlen or 180
        self._view_offset = 0
        self._auto_follow = True
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
        painter.setPen(QColor("#8b949e"))
        painter.drawText(
            int(outer.right()) - 250,
            int(outer.top()),
            "Wheel: zoom  Drag: pan  Double click: latest",
        )
        painter.drawText(
            int(outer.right()) - 250,
            int(outer.top()) + 16,
            f"View: {self._effective_view_span()}/{len(self._samples)} samples",
        )

        self._draw_grid(painter, top_rect)
        self._draw_grid(painter, bottom_rect)

        self._draw_axis_labels(
            painter,
            bottom_rect,
            ("100%", "75", "50", "25", "-10C"),
        )

        temp_min, temp_max = self._series_range(("outlet", "stage", "target"), 20.0, 70.0)
        self._draw_axis_labels(painter, top_rect, self._temperature_axis_labels(temp_min, temp_max))
        self._draw_series(painter, top_rect, "outlet", temp_min, temp_max, QColor("#ff6b6b"))
        self._draw_series(painter, top_rect, "stage", temp_min, temp_max, QColor("#4ecdc4"))
        self._draw_series(painter, top_rect, "target", temp_min, temp_max, QColor("#ffe66d"))
        self._draw_series(painter, bottom_rect, "output", 0.0, 100.0, QColor("#7bd389"))
        self._draw_series(painter, bottom_rect, "error", -10.0, 20.0, QColor("#a78bfa"))
        self._draw_series(painter, bottom_rect, "integral", 0.0, 100.0, QColor("#f59e0b"))
        self._draw_legend(
            painter,
            outer.left(),
            [
                ("PB11 Outlet", QColor("#ff6b6b")),
                ("PB10 Stage", QColor("#4ecdc4")),
                ("Outlet Target", QColor("#ffe66d")),
                ("Power", QColor("#7bd389")),
                ("Outlet Error", QColor("#a78bfa")),
                ("Integral", QColor("#f59e0b")),
            ],
        )

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

    def _series_range(self, names: tuple[str, ...], default_min: float, default_max: float) -> tuple[float, float]:
        values = [
            sample[name]
            for sample in self._visible_samples()
            for name in names
            if name in sample and self._visible.get(name, True)
        ]
        if not values:
            return default_min, default_max

        high = max(default_max, max(values) + 5.0)
        low = min(default_min, min(values) - 5.0)
        low = (low // 10.0) * 10.0
        high = ((high + 9.999) // 10.0) * 10.0
        if high <= low:
            high = low + 10.0
        return low, high

    def _temperature_axis_labels(self, min_y: float, max_y: float) -> tuple[str, ...]:
        return tuple(f"{max_y - (max_y - min_y) * i / 4:.0f}C" for i in range(5))

    def _draw_legend(self, painter: QPainter, left: float, items: list[tuple[str, QColor]]) -> None:
        x = int(left)
        for label, color in items:
            painter.setPen(color)
            painter.drawText(x, self.height() - 8, label)
            x += max(70, len(label) * 8 + 18)

    def _draw_series(
        self,
        painter: QPainter,
        rect: QRectF,
        name: str,
        min_y: float,
        max_y: float,
        color: QColor,
    ) -> None:
        samples = self._visible_samples()
        if (not self._visible.get(name, True)) or (len(samples) < 2):
            return

        path = QPainterPath()
        sample_count = len(samples)
        for i, sample in enumerate(samples):
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

    def wheelEvent(self, event: QWheelEvent) -> None:  # type: ignore[override]
        if not self._samples:
            return

        delta = event.angleDelta().y()
        if delta == 0:
            delta = event.pixelDelta().y()
        if delta == 0:
            event.accept()
            return

        self._zoom_view(0.75 if delta > 0 else 2.0)
        event.accept()

    def mousePressEvent(self, event: QMouseEvent) -> None:  # type: ignore[override]
        if event.button() == Qt.MouseButton.LeftButton:
            self._drag_last_x = event.position().x()
            event.accept()

    def mouseMoveEvent(self, event: QMouseEvent) -> None:  # type: ignore[override]
        if self._drag_last_x is None or not (event.buttons() & Qt.MouseButton.LeftButton):
            return

        span = self._effective_view_span()
        width = max(1, self.width())
        dx = event.position().x() - self._drag_last_x
        sample_delta = int(round(dx / width * span))
        if sample_delta != 0:
            self._view_offset += sample_delta
            self._auto_follow = False
            self._clamp_view()
            self._drag_last_x = event.position().x()
            self.update()
        event.accept()

    def mouseReleaseEvent(self, event: QMouseEvent) -> None:  # type: ignore[override]
        if event.button() == Qt.MouseButton.LeftButton:
            self._drag_last_x = None
            self._auto_follow = self._view_offset == 0
            event.accept()

    def mouseDoubleClickEvent(self, event: QMouseEvent) -> None:  # type: ignore[override]
        if event.button() == Qt.MouseButton.LeftButton:
            self.reset_view()
            event.accept()

    def _zoom_view(self, factor: float) -> None:
        if not self._samples:
            return

        max_span = self._samples.maxlen or 180
        old_span = max(1, self._view_span)
        new_span = int(round(old_span * factor))
        self._view_span = max(10, min(max_span, new_span))
        if factor > 1.0:
            self._auto_follow = self._view_offset == 0
        else:
            self._auto_follow = False
        self._clamp_view()
        self.update()

    def _effective_view_span(self) -> int:
        return max(1, min(len(self._samples), self._view_span))

    def _clamp_view(self) -> None:
        max_offset = max(0, len(self._samples) - self._effective_view_span())
        self._view_offset = max(0, min(self._view_offset, max_offset))

    def _visible_samples(self) -> list[dict[str, float]]:
        if not self._samples:
            return []

        self._clamp_view()
        samples = list(self._samples)
        span = self._effective_view_span()
        end = len(samples) - self._view_offset
        start = max(0, end - span)
        return samples[start:end]
