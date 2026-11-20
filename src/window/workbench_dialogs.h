/**
 * @file workbench_dialogs.h
 * @brief Custom frameless dialogs, connection error modal, and window title bars declarations.
 */

#pragma once

#include <QDialog>
#include <QFrame>
#include <QWidget>
#include <QPoint>

class QVBoxLayout;
class QCloseEvent;
class QPaintEvent;
class QMouseEvent;

/** @brief Keep an accepted import visible until the worker reports its outcome. */
class ProfileImportDialog : public QDialog
{
public:
    using QDialog::QDialog;
    bool applying = false;

    /** @brief Allow cancellation only before an import has been submitted. */
    void reject() override;

protected:
    /** @brief Prevent window-manager close while a submitted import is pending. */
    void closeEvent(QCloseEvent *event) override;
};


/** @brief Paint a small theme-aware document mark for the profile picker. */
class ProfileDocumentIcon : public QWidget
{
public:
    /** @brief Reserve a compact decorative area without adding a keyboard stop. */
    explicit ProfileDocumentIcon(QWidget *parent = nullptr);

protected:
    /** @brief Draw the document with antialiased vector paths in the current theme. */
    void paintEvent(QPaintEvent *event) override;
};


/** @brief Provide a themeable dialog title bar with native system dragging where available. */
class DialogTitleBar : public QFrame
{
public:
    /** @brief Add the dialog title and an accessible close action. */
    explicit DialogTitleBar(QDialog *dialog);

protected:
    /** @brief Start compositor-managed movement, retaining a fallback for unsupported platforms. */
    void mousePressEvent(QMouseEvent *event) override;

    /** @brief Move the dialog when native system movement is unavailable. */
    void mouseMoveEvent(QMouseEvent *event) override;

    /** @brief End fallback movement on pointer release. */
    void mouseReleaseEvent(QMouseEvent *event) override;

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
QVBoxLayout *addDialogTitleBar(QDialog *dialog, QVBoxLayout *layout);


/** @brief Paint a compact theme-aware alert icon. */
class ConnectionAlertIcon : public QWidget
{
public:
    explicit ConnectionAlertIcon(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
};


/** @brief Compact modal dialog presenting connection failure directly and cleanly. */
class ConnectionErrorDialog : public QDialog
{
public:
    explicit ConnectionErrorDialog(const QString &titleText, const QString &messageText, QWidget *parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QPoint offset;
    bool dragging = false;
};
