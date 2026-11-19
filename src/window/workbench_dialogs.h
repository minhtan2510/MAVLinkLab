/**
 * @file workbench_dialogs.h
 * @brief Custom frameless dialogs, connection error modal, and window title bars.
 */

#pragma once

#include "workbench_common.h"

/** @brief Keep an accepted import visible until the worker reports its outcome. */
class ProfileImportDialog : public QDialog
{
public:
    using QDialog::QDialog;
    bool applying = false;

    /** @brief Allow cancellation only before an import has been submitted. */
    void reject() override
    {
        if (!applying)
        {
            QDialog::reject();
        }
    }

protected:
    /** @brief Prevent window-manager close while a submitted import is pending. */
    void closeEvent(QCloseEvent *event) override
    {
        if (applying)
        {
            event->ignore();
            return;
        }
        QDialog::closeEvent(event);
    }
};


/** @brief Paint a small theme-aware document mark for the profile picker. */
class ProfileDocumentIcon : public QWidget
{
public:
    /** @brief Reserve a compact decorative area without adding a keyboard stop. */
    explicit ProfileDocumentIcon(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedSize(40, 40);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    /** @brief Draw the document with antialiased vector paths in the current theme. */
    void paintEvent(QPaintEvent *) override
    {
        const bool dark = qApp->property("darkTheme").toBool();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(dark ? "#30355d" : "#eeedff"));
        painter.drawRoundedRect(QRectF(0, 0, 40, 40), 10, 10);
        painter.setPen(QPen(QColor(dark ? "#b4adff" : "#6254d8"), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        QPainterPath page;
        page.moveTo(13, 9);
        page.lineTo(22, 9);
        page.lineTo(28, 15);
        page.lineTo(28, 30);
        page.lineTo(13, 30);
        page.closeSubpath();
        painter.drawPath(page);
        painter.drawLine(QPointF(22, 9), QPointF(22, 15));
        painter.drawLine(QPointF(22, 15), QPointF(28, 15));
        painter.drawLine(QPointF(17, 21), QPointF(24, 21));
        painter.drawLine(QPointF(17, 25), QPointF(22, 25));
    }
};


/** @brief Provide a themeable dialog title bar with native system dragging where available. */
class DialogTitleBar : public QFrame
{
public:
    /** @brief Add the dialog title and an accessible close action. */
    explicit DialogTitleBar(QDialog *dialog) : QFrame(dialog)
    {
        setObjectName("dialogTitleBar");
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(20, 16, 16, 8);
        row->setSpacing(12);
        auto *title = new QLabel(dialog->windowTitle(), this);
        title->setObjectName("dialogWindowTitle");
        title->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(title, 1);
        connect(dialog, &QWidget::windowTitleChanged, title, &QLabel::setText);
        auto *close = new QPushButton(QString::fromUtf8("×"), this);
        close->setObjectName("closeDialog");
        close->setAccessibleName("Close dialog");
        close->setFixedSize(28, 28);
        close->setAutoDefault(false);
        row->addWidget(close);
        connect(close, &QPushButton::clicked, dialog, &QDialog::reject);
    }

protected:
    /** @brief Start compositor-managed movement, retaining a fallback for unsupported platforms. */
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            offset = event->globalPosition().toPoint() - window()->frameGeometry().topLeft();
            dragging = !window()->windowHandle() || !window()->windowHandle()->startSystemMove();
            event->accept();
            return;
        }
        QFrame::mousePressEvent(event);
    }

    /** @brief Move the dialog when native system movement is unavailable. */
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (dragging && (event->buttons() & Qt::LeftButton))
        {
            window()->move(event->globalPosition().toPoint() - offset);
            event->accept();
            return;
        }
        QFrame::mouseMoveEvent(event);
    }

    /** @brief End fallback movement on pointer release. */
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        dragging = false;
        QFrame::mouseReleaseEvent(event);
    }

private:
    QPoint offset;
    bool dragging = false;
};


/**
 * @brief Create a full-width themed header above an inset dialog body.
 * @param dialog Dialog whose native decorations are replaced.
 * @param layout Empty outer layout containing the header and body.
 * @return Body layout retaining the original content margins and spacing.
 */
inline QVBoxLayout *addDialogTitleBar(QDialog *dialog, QVBoxLayout *layout)
{
    dialog->setWindowFlag(Qt::FramelessWindowHint);
    dialog->setSizeGripEnabled(true);
    const QMargins margins = layout->contentsMargins();
    const int spacing = layout->spacing();
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addWidget(new DialogTitleBar(dialog));
    auto *dividerRow = new QHBoxLayout;
    dividerRow->setContentsMargins(margins.left(), 0, margins.right(), 0);
    auto *divider = new QFrame(dialog);
    divider->setObjectName("dialogTitleDivider");
    divider->setFixedHeight(1);
    dividerRow->addWidget(divider);
    layout->addLayout(dividerRow);
    auto *body = new QWidget(dialog);
    auto *content = new QVBoxLayout(body);
    content->setContentsMargins(margins.left(), 8, margins.right(), margins.bottom());
    content->setSpacing(spacing);
    layout->addWidget(body, 1);
    return content;
}


/** @brief Paint a compact theme-aware alert icon. */
class ConnectionAlertIcon : public QWidget
{
public:
    explicit ConnectionAlertIcon(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedSize(32, 32);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const bool dark = qApp->property("darkTheme").toBool();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(dark ? "#3b1e26" : "#fef2f2"));
        painter.drawRoundedRect(QRectF(0, 0, 32, 32), 8, 8);

        // Warning triangle
        painter.setPen(QPen(QColor(dark ? "#f87171" : "#dc2626"), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        QPainterPath triangle;
        triangle.moveTo(16, 7);
        triangle.lineTo(25, 24);
        triangle.lineTo(7, 24);
        triangle.closeSubpath();
        painter.drawPath(triangle);

        // Exclamation mark
        painter.drawLine(QPointF(16, 13), QPointF(16, 18));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(dark ? "#f87171" : "#dc2626"));
        painter.drawEllipse(QRectF(15, 20.5, 2, 2));
    }
};


/** @brief Compact modal dialog presenting connection failure directly and cleanly. */
class ConnectionErrorDialog : public QDialog
{
public:
    explicit ConnectionErrorDialog(const QString &titleText, const QString &messageText, QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setObjectName("connectionErrorDialog");
        setWindowTitle(titleText);
        setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setFixedWidth(380);

        auto *rootLayout = new QVBoxLayout(this);
        rootLayout->setContentsMargins(4, 4, 4, 4);

        auto *card = new QFrame(this);
        card->setObjectName("connectionErrorCard");
        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(18, 18, 18, 16);
        layout->setSpacing(14);

        // Content row: Icon + Text
        auto *contentRow = new QHBoxLayout;
        contentRow->setSpacing(12);
        contentRow->setAlignment(Qt::AlignTop);

        contentRow->addWidget(new ConnectionAlertIcon(card), 0, Qt::AlignTop);

        auto *textLayout = new QVBoxLayout;
        textLayout->setSpacing(4);

        auto *headline = new QLabel(titleText, card);
        headline->setObjectName("dialogHeadline");
        QFont f = font();
        f.setPixelSize(14);
        f.setWeight(QFont::DemiBold);
        headline->setFont(f);
        textLayout->addWidget(headline);

        auto *message = new QLabel(messageText, card);
        message->setObjectName("connectionErrorMessage");
        message->setWordWrap(true);
        QFont mf = font();
        mf.setPixelSize(12);
        message->setFont(mf);
        textLayout->addWidget(message);

        contentRow->addLayout(textLayout, 1);
        layout->addLayout(contentRow);

        // Button row: right-aligned compact OK button
        auto *buttonRow = new QHBoxLayout;
        buttonRow->addStretch(1);
        auto *okButton = new QPushButton("OK", card);
        okButton->setObjectName("dismissConnectionError");
        okButton->setFixedSize(72, 28);
        okButton->setDefault(true);
        connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
        buttonRow->addWidget(okButton);
        layout->addLayout(buttonRow);

        rootLayout->addWidget(card);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            offset = event->globalPosition().toPoint() - frameGeometry().topLeft();
            dragging = !windowHandle() || !windowHandle()->startSystemMove();
            event->accept();
            return;
        }
        QDialog::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (dragging && (event->buttons() & Qt::LeftButton))
        {
            move(event->globalPosition().toPoint() - offset);
            event->accept();
            return;
        }
        QDialog::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        dragging = false;
        QDialog::mouseReleaseEvent(event);
    }

private:
    QPoint offset;
    bool dragging = false;
};

