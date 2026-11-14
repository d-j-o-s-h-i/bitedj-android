#pragma once

#include <QEvent>
#include <QList>
#include <QPointer>
#include <QScroller>
#include <QWidget>
#include <utility>

// Qt starts gestures on a widget's ancestors as well as on the touch target.
// For nested scroll areas this starts both scrollers. Mask ancestor gestures
// during a touch that begins inside an inner scroller, then restore them once
// that touch ends. The mask must be applied before QApplication::notify().
class NestedScrollGestureGuard {
  public:
    void beforeNotify(QObject* target, QEvent* event) {
        if (event->type() != QEvent::TouchBegin || !m_masked.isEmpty()) {
            return;
        }
        auto* widget = qobject_cast<QWidget*>(target);
        bool foundInnerScroller = false;
        for (; widget; widget = widget->parentWidget()) {
            if (!QScroller::hasScroller(widget)) {
                continue;
            }
            const auto gesture = QScroller::grabbedGesture(widget);
            if (gesture == Qt::GestureType(0) || gesture == Qt::CustomGesture) {
                continue;
            }
            if (foundInnerScroller) {
                widget->grabGesture(gesture, Qt::DontStartGestureOnChildren);
                m_masked.append(widget);
            } else {
                foundInnerScroller = true;
            }
        }
    }

    void afterNotify(QEvent* event) {
        if (event->type() != QEvent::TouchEnd &&
                event->type() != QEvent::TouchCancel) {
            return;
        }
        for (const auto& widget : std::as_const(m_masked)) {
            if (widget) {
                const auto gesture = QScroller::grabbedGesture(widget);
                if (gesture != Qt::GestureType(0) && gesture != Qt::CustomGesture) {
                    widget->grabGesture(gesture);
                }
            }
        }
        m_masked.clear();
    }

  private:
    QList<QPointer<QWidget>> m_masked;
};
