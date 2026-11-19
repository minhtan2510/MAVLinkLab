/**
 * @file window_editor.cpp
 * @brief MainWindow MAVLink message catalog search dialog and payload field editor.
 */

#include "window.h"
#include "window_private.h"
#include "mavlink_enums.h"
#include <QDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QHeaderView>
#include <QDialogButtonBox>
#include <QScrollBar>
#include <QMessageBox>
#include <QCompleter>

/**
 * @brief Event filter for message editor dialog to complete the active field or dropdown
 * and clear its focus when Enter is pressed, preventing premature dialog submission.
 */
class MessageDialogKeyFilter : public QObject
{
public:
    /**
     * @brief Construct key filter with reference to the dialog's save button and commit callback.
     * @param saveBtn Save/Add message button.
     * @param commitCallback Callback to commit active table cell editors.
     * @param parent Optional parent QObject.
     */
    explicit MessageDialogKeyFilter(QPushButton *saveBtn, std::function<void()> commitCallback, QObject *parent = nullptr)
        : QObject(parent), saveButton(saveBtn), onCommit(std::move(commitCallback))
    {
    }

protected:
    /**
     * @brief Intercept Enter/Return key to finish editing current field and clear focus without submitting dialog.
     * @param watched Watched dialog object.
     * @param event Key press event.
     * @return True if event is consumed, false otherwise.
     */
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::KeyPress)
        {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
            {
                QWidget *focused = QApplication::focusWidget();
                if (focused == saveButton)
                {
                    return false;
                }
                if (qobject_cast<QPushButton *>(focused))
                {
                    return false;
                }
                if (focused)
                {
                    if (auto *line = qobject_cast<QLineEdit *>(focused))
                    {
                        emit line->editingFinished();
                    }
                    else if (auto *spin = qobject_cast<QAbstractSpinBox *>(focused))
                    {
                        spin->interpretText();
                    }
                    focused->clearFocus();
                    if (auto *parent = focused->parentWidget())
                    {
                        parent->clearFocus();
                    }
                }
                if (onCommit)
                {
                    onCommit();
                }
                if (QWidget *currentFocus = QApplication::focusWidget())
                {
                    currentFocus->clearFocus();
                }
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QPushButton *saveButton = nullptr;
    std::function<void()> onCommit;
};

/** @brief Configure message identity, sender and payload with contextual validation feedback. */
void MainWindow::editMessage(bool edit)
{
    Mav::TxEntry entry;
    if (edit)
    {
        if (!latest)
        {
            return;
        }
        const auto key = selectedTx();
        const auto found = std::find_if(latest->tx.begin(), latest->tx.end(), [&](const auto &item)
        {
            return item.entry.key == key;
        });
        if (found == latest->tx.end())
        {
            return;
        }
        entry = found->entry;
    }
    else
    {
        entry.key = nextKey++;
        entry.sysid = static_cast<uint8_t>(defaultSysIdBox ? defaultSysIdBox->value() : 1);
        entry.compid = static_cast<uint8_t>(defaultCompIdBox ? defaultCompIdBox->value() : 1);
        entry.version1 = (defaultVersionBox && defaultVersionBox->currentIndex() == 1);
    }
    QDialog dialog(this);
    dialog.setObjectName("messageDialog");
    dialog.setWindowTitle(edit ? "Edit message" : "Add MAVLink message");
    dialog.setMinimumSize(880, 600);
    dialog.resize(1000, 680);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(12);
    layout = addDialogTitleBar(&dialog, layout);
    auto *body = new QSplitter(Qt::Horizontal);
    body->setObjectName("messageSplitter");
    body->setChildrenCollapsible(false);
    body->setHandleWidth(2);
    auto *catalogWidget = new QWidget;
    catalogWidget->setMinimumWidth(240);
    auto *catalogPane = new QVBoxLayout(catalogWidget);
    catalogPane->setContentsMargins(0, 0, 0, 0);
    catalogPane->setSpacing(10);
    auto *catalogHeading = new QHBoxLayout;
    auto *catalogTitle = new QLabel("MESSAGE CATALOG");
    catalogTitle->setObjectName("sectionTitle");
    catalogHeading->addWidget(catalogTitle);
    auto *resultCount = new QLabel;
    resultCount->setObjectName("catalogResultCount");
    resultCount->setMinimumWidth(82);
    resultCount->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    catalogHeading->addStretch();
    catalogHeading->addWidget(resultCount);
    catalogPane->addLayout(catalogHeading);
    auto *search = new CatalogSearch;
    search->setPlaceholderText("Search ID or name…");
    search->setAccessibleName("Search MAVLink catalog");
    search->setObjectName("catalogSearch");
    catalogPane->addWidget(search);
    auto *catalogList = new QListWidget;
    catalogList->setObjectName("messageCatalog");
    catalogList->setItemDelegate(new CatalogDelegate(catalogList));
    catalogList->setMouseTracking(true);
    catalogList->setTextElideMode(Qt::ElideRight);
    catalogList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    for (const auto &info : Mav::catalog())
    {
        auto *item = new CatalogItem(QString("[%1] %2").arg(info.msgid).arg(info.name), catalogList);
        item->setData(Qt::UserRole, info.msgid);
        item->setData(Qt::UserRole + 3, QString::fromLatin1(info.name));
    }
    catalogPane->addWidget(catalogList, 1);
    auto *noMatches = new QLabel("No matching messages");
    noMatches->setObjectName("catalogEmpty");
    noMatches->setAlignment(Qt::AlignCenter);
    noMatches->hide();
    catalogPane->addWidget(noMatches, 1);
    resultCount->setText(QString::number(catalogList->count()) + " results");
    body->addWidget(catalogWidget);
    auto *editorWidget = new QWidget;
    editorWidget->setMinimumWidth(540);
    auto *editor = new QVBoxLayout(editorWidget);
    editor->setContentsMargins(0, 0, 0, 0);
    editor->setSpacing(10);

    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(8);

    auto *messageTitle = new QLabel;
    messageTitle->setObjectName("selectedMessageTitle");
    messageTitle->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    titleRow->addWidget(messageTitle, 0, Qt::AlignVCenter);

    auto *payloadSizeBadge = new QLabel;
    payloadSizeBadge->setObjectName("messagePayloadSize");
    payloadSizeBadge->setFixedHeight(24);
    payloadSizeBadge->setAlignment(Qt::AlignCenter);
    payloadSizeBadge->hide();
    titleRow->addWidget(payloadSizeBadge, 0, Qt::AlignVCenter);

    titleRow->addStretch();
    editor->addLayout(titleRow);
    auto *form = new QHBoxLayout;
    auto *sysid = new WorkbenchSpinBox<QSpinBox>;
    sysid->setObjectName("messageSysId");
    auto *compid = new WorkbenchComboBox;
    compid->setObjectName("messageCompId");
    compid->setEditable(true);

    const auto compEntries = Mav::enumEntries("MAV_COMPONENT");
    QStringList compSuggestions;
    compSuggestions.reserve(int(compEntries.size()));
    for (const auto &entryItem : compEntries)
    {
        const QString name = QString::fromLatin1(entryItem.name);
        compSuggestions << name;
        compid->addItem(name, QVariant::fromValue<qulonglong>(entryItem.value));
        compid->setItemData(compid->count() - 1, QString::number(entryItem.value), Qt::ToolTipRole);
    }

    class CompIdLineFilter : public QObject
    {
    public:
        explicit CompIdLineFilter(QLineEdit *line)
            : QObject(line), m_line(line)
        {
        }

    protected:
        bool eventFilter(QObject *watched, QEvent *event) override
        {
            if (event->type() == QEvent::FocusOut || event->type() == QEvent::Show)
            {
                if (m_line && !m_line->hasFocus())
                {
                    m_line->setCursorPosition(0);
                }
            }
            return QObject::eventFilter(watched, event);
        }

    private:
        QLineEdit *m_line = nullptr;
    };

    if (auto *line = compid->lineEdit())
    {
        line->setObjectName("messageCompIdEditor");
        line->setTextMargins(0, 0, 18, 0);
        auto *completer = new QCompleter(compSuggestions, line);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setFilterMode(Qt::MatchContains);
        line->setCompleter(completer);
        line->installEventFilter(new CompIdLineFilter(line));
    }

    const auto updateCompIdTooltipAndNormalize = [compid, &compEntries](bool normalizeNumber)
    {
        QString text = compid->currentText().trimmed();
        if (text.isEmpty())
        {
            compid->setToolTip(QString());
            return;
        }

        const int paren = text.indexOf('(');
        if (paren > 0)
        {
            text = text.left(paren).trimmed();
        }

        const Mav::EnumEntry *matched = nullptr;
        bool ok = false;
        const uint64_t numVal = text.toULongLong(&ok, 0);

        if (ok)
        {
            for (const auto &e : compEntries)
            {
                if (e.value == numVal)
                {
                    matched = &e;
                    break;
                }
            }
            if (normalizeNumber && matched)
            {
                const QSignalBlocker blocker(compid);
                compid->setEditText(QString::fromLatin1(matched->name));
            }
        }
        else
        {
            for (const auto &e : compEntries)
            {
                if (text.compare(e.name, Qt::CaseInsensitive) == 0)
                {
                    matched = &e;
                    break;
                }
            }
        }

        if (matched)
        {
            compid->setToolTip(QString("%1 = %2").arg(matched->name).arg(matched->value));
        }
        else if (ok && numVal <= 255)
        {
            compid->setToolTip(QString("Component ID %1").arg(numVal));
        }
        else
        {
            compid->setToolTip(QString());
        }

        if (auto *line = compid->lineEdit())
        {
            if (!line->hasFocus())
            {
                line->setCursorPosition(0);
            }
        }
    };

    const std::string initialCompName = Mav::lookupEnumNameByType("MAV_COMPONENT", entry.compid);
    if (!initialCompName.empty())
    {
        const int initialIdx = compid->findText(QString::fromLatin1(initialCompName));
        if (initialIdx >= 0)
        {
            compid->setCurrentIndex(initialIdx);
        }
        else
        {
            compid->setEditText(QString::fromLatin1(initialCompName));
        }
    }
    else
    {
        const int initialIdx = compid->findData(quint64(entry.compid));
        if (initialIdx >= 0)
        {
            compid->setCurrentIndex(initialIdx);
        }
        else
        {
            compid->setEditText(QString::number(entry.compid));
        }
    }
    updateCompIdTooltipAndNormalize(false);
    if (auto *line = compid->lineEdit())
    {
        line->setCursorPosition(0);
    }

    QObject::connect(compid, &QComboBox::currentTextChanged, [updateCompIdTooltipAndNormalize](const QString &)
    {
        updateCompIdTooltipAndNormalize(false);
    });
    if (auto *line = compid->lineEdit())
    {
        QObject::connect(line, &QLineEdit::editingFinished, [updateCompIdTooltipAndNormalize, line]()
        {
            updateCompIdTooltipAndNormalize(true);
            line->setCursorPosition(0);
        });
    }
    QObject::connect(compid, QOverload<int>::of(&QComboBox::activated), [updateCompIdTooltipAndNormalize, compid](int)
    {
        updateCompIdTooltipAndNormalize(true);
        if (auto *line = compid->lineEdit())
        {
            line->setCursorPosition(0);
        }
    });

    sysid->setRange(1, 255);
    sysid->setValue(entry.sysid);
    auto *version = new WorkbenchComboBox;
    version->setObjectName("messageVersion");
    version->addItems({"MAVLink v2", "MAVLink v1"});
    version->setCurrentIndex(entry.version1 ? 1 : 0);
    auto *rate = new WorkbenchSpinBox<QDoubleSpinBox>;
    rate->setDecimals(0);
    rate->setRange(1, 1000);
    rate->setSingleStep(1);
    rate->setValue(entry.hz);
    const auto fieldControl = [&](const QString &name, QWidget *control, int stretch = 1, int fixedWidth = 0)
    {
        auto *field = new QVBoxLayout;
        field->setSpacing(6);
        auto *label = new QLabel(name);
        label->setObjectName("serialFieldLabel");
        label->setBuddy(control);
        field->addWidget(label);
        control->setFixedHeight(34);
        if (fixedWidth > 0)
        {
            control->setFixedWidth(fixedWidth);
            control->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            field->addWidget(control);
            form->addLayout(field, 0);
        }
        else
        {
            control->setMinimumWidth(0);
            control->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            field->addWidget(control);
            form->addLayout(field, stretch);
        }
        control->setAccessibleName(name);
    };
    fieldControl("System ID", sysid, 0, 110);
    fieldControl("Component ID", compid, 1, 0);
    form->addSpacing(14);
    fieldControl("Rate (Hz)", rate, 0, 110);
    editor->addLayout(form);
    auto *parameterSearch = new CatalogSearch;
    parameterSearch->setObjectName("parameterSearch");
    parameterSearch->setPlaceholderText("Filter parameters…");
    parameterSearch->setAccessibleName("Filter message parameters");
    parameterSearch->hide();
    editor->addWidget(parameterSearch);
    auto *fields = table({"Parameter", "Type", "Value"});
    fields->setObjectName("payloadFields");
    fields->setItemDelegateForColumn(0, new PayloadIdentityDelegate(fields));
    fields->setItemDelegateForColumn(1, new PayloadIdentityDelegate(fields));
    fields->setEditTriggers(QAbstractItemView::AllEditTriggers);
    fields->setSelectionBehavior(QAbstractItemView::SelectItems);
    fields->setAlternatingRowColors(false);
    fields->setMouseTracking(true);
    fields->verticalHeader()->setDefaultSectionSize(40);
    fields->horizontalHeader()->setFixedHeight(36);
    fields->horizontalHeader()->setMinimumSectionSize(90);
    fields->setColumnWidth(0, 190);
    fields->setColumnWidth(1, 105);
    editor->addWidget(fields, 1);
    auto *hint = new QLabel;
    hint->setWordWrap(true);
    hint->setObjectName("payloadHint");
    hint->hide();
    auto *hintRow = new QHBoxLayout;
    hintRow->addWidget(hint, 1);
    auto *arrayButton = new QPushButton("Edit array…");
    arrayButton->setObjectName("editPayloadArray");
    arrayButton->setAutoDefault(false);
    arrayButton->hide();
    hintRow->addWidget(arrayButton);
    editor->addLayout(hintRow);
    auto *error = new QLabel;
    error->setObjectName("validationError");
    error->setWordWrap(true);
    error->hide();
    editor->addWidget(error);
    body->addWidget(editorWidget);
    body->setSizes({270, 673});
    body->setStretchFactor(0, 0);
    body->setStretchFactor(1, 1);
    layout->addWidget(body, 1);
    auto *footerDivider = new QFrame;
    footerDivider->setObjectName("messageFooterDivider");
    footerDivider->setFixedHeight(1);
    layout->addWidget(footerDivider);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->setLayoutDirection(Qt::RightToLeft);
    QWidget::setTabOrder(buttons->button(QDialogButtonBox::Cancel), buttons->button(QDialogButtonBox::Save));
    auto *saveButton = buttons->button(QDialogButtonBox::Save);
    saveButton->setProperty("role", "primary");
    saveButton->setMinimumWidth(120);
    saveButton->setText(edit ? "Save changes" : "Add message");
    saveButton->setToolTip("Ctrl+Click to fill empty fields with 0");
    auto *footerRow = new QHBoxLayout;
    footerRow->setContentsMargins(0, 0, 0, 0);
    footerRow->setSpacing(12);

    auto *protocolLayout = new QHBoxLayout;
    protocolLayout->setSpacing(8);
    auto *protocolLabel = new QLabel("MAVLink Version:");
    protocolLabel->setObjectName("secondaryText");
    protocolLabel->setBuddy(version);
    protocolLayout->addWidget(protocolLabel, 0, Qt::AlignVCenter);
    version->setFixedWidth(150);
    version->setFixedHeight(32);
    version->setAccessibleName("MAVLink Version");
    protocolLayout->addWidget(version, 0, Qt::AlignVCenter);

    auto *loadEstimate = new QLabel;
    loadEstimate->setObjectName("messageLoadEstimate");
    loadEstimate->setFixedHeight(32);
    loadEstimate->setAlignment(Qt::AlignCenter);
    loadEstimate->hide();
    protocolLayout->addSpacing(8);
    protocolLayout->addWidget(loadEstimate, 0, Qt::AlignVCenter);

    footerRow->addLayout(protocolLayout);

    footerRow->addStretch();
    buttons->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    footerRow->addWidget(buttons, 0, Qt::AlignRight | Qt::AlignVCenter);
    layout->addLayout(footerRow);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    const mavlink_message_info_t *selectedInfo = nullptr;

    const PortConfig activeConfig = currentPortConfig();
    const int currentBaud = activeConfig.baud > 0 ? activeConfig.baud : 115200;
    const double bitsPerByte = framingBitsPerByte(activeConfig);
    const QString framingName = framingFormatName(activeConfig);
    const bool isDark = property("darkTheme").isValid() ?
                        property("darkTheme").toBool() :
                        (themeBox && themeBox->currentIndex() == 2);
    const auto updateLoadEstimate = [&]()
    {
        if (!selectedInfo)
        {
            loadEstimate->hide();
            return;
        }
        const auto *metadata = mavlink_get_msg_entry(selectedInfo->msgid);
        if (!metadata)
        {
            loadEstimate->hide();
            return;
        }
        Mav::TxEntry temp{};
        temp.id = selectedInfo->msgid;
        temp.version1 = (version->currentIndex() == 1);
        temp.hz = rate->value();
        temp.payload.assign(metadata->max_msg_len, 0);
        const double bps = Mav::estimateEntryThroughput(temp);
        const double util = currentBaud > 0 ? (bps * bitsPerByte / currentBaud) * 100.0 : 0.0;
        loadEstimate->show();

        const QString framingDetail = QString("%1 (%2 b/B: 1 start + %3 data + %4 parity + %5 stop)")
                                          .arg(framingName)
                                          .arg(bitsPerByte, 0, 'f', (bitsPerByte == std::floor(bitsPerByte) ? 0 : 1))
                                          .arg(int(activeConfig.data))
                                          .arg(activeConfig.parity == QSerialPort::NoParity ? 0 : 1)
                                          .arg(activeConfig.stop == QSerialPort::TwoStop ? 2.0 :
                                               activeConfig.stop == QSerialPort::OneAndHalfStop ? 1.5 : 1.0);

        const QString badgeText = (util > 90.0) ?
            QString("Est. load: ~%1 (%2% at %3 %4 - High!)")
                .arg(QString::fromStdString(Mav::formatByteRate(bps)))
                .arg(util, 0, 'f', 2)
                .arg(currentBaud)
                .arg(framingName) :
            QString("Est. load: ~%1 (%2% at %3 %4)")
                .arg(QString::fromStdString(Mav::formatByteRate(bps)))
                .arg(util, 0, 'f', 2)
                .arg(currentBaud)
                .arg(framingName);

        const QString tooltipText = QString("Estimated wire throughput: ~%1\n"
                                            "Channel utilization: %2% of %3 baud\n"
                                            "UART physical framing: %4\n"
                                            "MAVLink wire size: %5 bytes (%6 payload + %7 header/CRC)")
                                        .arg(QString::fromStdString(Mav::formatByteRate(bps)))
                                        .arg(util, 0, 'f', 2)
                                        .arg(currentBaud)
                                        .arg(framingDetail)
                                        .arg(Mav::frameSize(temp))
                                        .arg(metadata->max_msg_len)
                                        .arg(temp.version1 ? 8 : 12);

        loadEstimate->setText(badgeText);
        loadEstimate->setToolTip(tooltipText);

        if (util > 90.0)
        {
            loadEstimate->setStyleSheet(isDark ?
                "color: #f87171; font-size: 11px; font-weight: 600; padding: 2px 10px; background: #2e1215; border: 1px solid #7f1d1d; border-radius: 6px;" :
                "color: #dc2626; font-size: 11px; font-weight: 600; padding: 2px 10px; background: #fef2f2; border: 1px solid #fecaca; border-radius: 6px;");
        }
        else
        {
            loadEstimate->setStyleSheet(isDark ?
                "color: #94a3b8; font-size: 11px; padding: 2px 10px; background: #1e293b; border: 1px solid #334155; border-radius: 6px;" :
                "color: #475569; font-size: 11px; padding: 2px 10px; background: #f1f5f9; border: 1px solid #cbd5e1; border-radius: 6px;");
        }
    };
    QObject::connect(rate, &QDoubleSpinBox::valueChanged, updateLoadEstimate);
    QObject::connect(version, QOverload<int>::of(&QComboBox::currentIndexChanged), updateLoadEstimate);

    std::map<uint32_t, QStringList> drafts;
    bool loadingFields = false;
    bool submissionAttempted = false;
    // Commit an active native editor before saving a draft or opening another editor.
    const auto commitEditor = [&]()
    {
        auto *active = fields->findChild<QLineEdit *>("payloadValueEditor");
        if (active && active->isVisible())
        {
            auto *delegate = fields->itemDelegateForColumn(2);
            QWidget *target = active;
            if (auto *combo = qobject_cast<QComboBox *>(active->parentWidget()))
            {
                target = combo;
            }
            emit delegate->commitData(target);
            emit delegate->closeEditor(target, QAbstractItemDelegate::NoHint);
        }
    };
    const auto validateField = [&](int row)
    {
        auto *item = fields->item(row, 2);
        if (!selectedInfo || !item || !(item->flags() & Qt::ItemIsEditable))
        {
            return true;
        }
        const auto enumEntries = Mav::fieldEnumEntries(selectedInfo->msgid, selectedInfo->fields[row].name);
        const QString currentText = item->text().trimmed();
        const auto &field = selectedInfo->fields[row];
        const bool isCharArray = (field.type == MAVLINK_TYPE_CHAR && field.array_length);

        if (currentText.isEmpty() && !isCharArray)
        {
            const QSignalBlocker blocker(fields);
            const bool showError = submissionAttempted;
            item->setData(Qt::UserRole + 1, showError);
            const QString emptyReason = !enumEntries.empty() ?
                "Value required: please select or enter an enum name" :
                "Value required: please enter a value";
            item->setData(Qt::UserRole + 4, showError ? emptyReason : QString());
            item->setData(Qt::AccessibleDescriptionRole, showError ? emptyReason : QString());
            item->setToolTip(emptyReason);
            fields->setRowHeight(row, showError ? 60 : 40);
            return false;
        }

        if (!enumEntries.empty())
        {
            bool ok = false;
            const qulonglong numVal = currentText.toULongLong(&ok, 0);
            if (ok)
            {
                const std::string enumSymbol = Mav::lookupEnumName(selectedInfo->msgid, selectedInfo->fields[row].name, numVal);
                if (!enumSymbol.empty() && currentText != QString::fromLatin1(enumSymbol))
                {
                    const QSignalBlocker blocker(fields);
                    item->setText(QString::fromLatin1(enumSymbol));
                }
            }
        }
        const auto *metadata = mavlink_get_msg_entry(selectedInfo->msgid);
        Mav::Bytes probe(metadata->max_msg_len, 0);
        std::string reason;
        const bool valid = Mav::setField(probe, selectedInfo->fields[row], item->text().toUtf8().toStdString(), reason);
        const QSignalBlocker blocker(fields);
        item->setData(Qt::UserRole + 1, !valid);
        item->setData(Qt::UserRole + 4, QString::fromStdString(reason));
        item->setData(Qt::AccessibleDescriptionRole, QString::fromStdString(reason));
        if (valid)
        {
            if (!enumEntries.empty())
            {
                const QString currentText = item->text().trimmed();
                const Mav::EnumEntry *matched = nullptr;
                for (const auto &e : enumEntries)
                {
                    if (currentText.compare(e.name, Qt::CaseInsensitive) == 0)
                    {
                        matched = &e;
                        break;
                    }
                }
                if (!matched)
                {
                    bool ok = false;
                    const uint64_t numVal = currentText.toULongLong(&ok);
                    if (ok)
                    {
                        for (const auto &e : enumEntries)
                        {
                            if (e.value == numVal)
                            {
                                matched = &e;
                                break;
                            }
                        }
                    }
                    else
                    {
                        const auto valOpt = Mav::lookupEnumValue(currentText.toStdString());
                        if (valOpt.has_value())
                        {
                            for (const auto &e : enumEntries)
                            {
                                if (e.value == *valOpt)
                                {
                                    matched = &e;
                                    break;
                                }
                            }
                        }
                    }
                }
                if (matched)
                {
                    item->setToolTip(QString("%1 = %2").arg(matched->name).arg(matched->value));
                }
                else
                {
                    item->setToolTip(QString());
                }
            }
            else
            {
                item->setToolTip(QString());
            }
        }
        else
        {
            item->setToolTip(QString::fromStdString(reason));
        }
        fields->setRowHeight(row, valid ? 40 : 60);
        return valid;
    };
    const auto filterParameters = [&]()
    {
        const QString query = parameterSearch->text().trimmed();
        for (int row = 0; row < fields->rowCount(); ++row)
        {
            fields->setRowHidden(row, !fields->item(row, 0)->text().contains(query, Qt::CaseInsensitive));
        }
    };
    connect(parameterSearch, &QLineEdit::textChanged, &dialog, filterParameters);
    // Field-specific guidance replaces the permanent multi-line footer instructions.
    connect(fields, &QTableWidget::currentCellChanged, &dialog, [&](int row, int, int, int)
    {
        hint->clear();
        arrayButton->hide();
        if (selectedInfo && row >= 0 && row < int(selectedInfo->num_fields) && fields->item(row, 2))
        {
            const auto &field = selectedInfo->fields[row];
            if (!(fields->item(row, 2)->flags() & Qt::ItemIsEditable))
            {
                hint->setText("Read-only: protocol constant supplied by the official HEARTBEAT packer.");
            }
            else if (field.array_length)
            {
                arrayButton->show();
                hint->setText(field.type == MAVLINK_TYPE_CHAR ?
                    QString("UTF-8 text · maximum %1 bytes.").arg(field.array_length) :
                    QString("Enter exactly %1 comma-separated numeric values.").arg(field.array_length));
            }
            else
            {
                const auto enumType = Mav::fieldEnumType(selectedInfo->msgid, field.name);
                if (!enumType.empty())
                {
                    hint->setText(QString("Enum %1: accept symbolic name or numeric value (e.g. MAV_PARAM_TYPE_REAL32 or 9).")
                                  .arg(QString::fromStdString(enumType)));
                }
            }
        }
        hint->setVisible(!hint->text().isEmpty());
    });
    connect(fields, &QTableWidget::itemChanged, &dialog, [&](QTableWidgetItem *item)
    {
        if (!loadingFields && item->column() == 2)
        {
            validateField(item->row());
            error->clear();
            error->hide();
        }
    });
    connect(arrayButton, &QPushButton::clicked, &dialog, [&]()
    {
        commitEditor();
        const int row = fields->currentRow();
        if (!selectedInfo || row < 0 || row >= int(selectedInfo->num_fields))
        {
            return;
        }
        const auto &field = selectedInfo->fields[row];
        if (!field.array_length)
        {
            return;
        }
        QDialog arrayDialog(&dialog);
        arrayDialog.setObjectName("arrayDialog");
        arrayDialog.setWindowTitle(QString("Edit %1").arg(field.name));
        arrayDialog.resize(620, 380);
        arrayDialog.setMinimumSize(460, 300);
        auto *arrayLayout = new QVBoxLayout(&arrayDialog);
        arrayLayout->setContentsMargins(20, 12, 20, 16);
        arrayLayout = addDialogTitleBar(&arrayDialog, arrayLayout);
        auto *guidance = new QLabel(hint->text());
        guidance->setObjectName("payloadHint");
        guidance->setWordWrap(true);
        arrayLayout->addWidget(guidance);
        auto *input = new QPlainTextEdit;
        input->setObjectName("arrayValueEditor");
        input->setAccessibleName(QString::fromLatin1(field.name));
        input->setFont(cachedFixedFont());
        input->setPlainText(fields->item(row, 2)->text());
        arrayLayout->addWidget(input, 1);
        auto *arrayError = new QLabel;
        arrayError->setObjectName("validationError");
        arrayError->setWordWrap(true);
        arrayError->hide();
        arrayLayout->addWidget(arrayError);
        auto *actions = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Save);
        actions->setLayoutDirection(Qt::RightToLeft);
        QWidget::setTabOrder(actions->button(QDialogButtonBox::Cancel), actions->button(QDialogButtonBox::Save));
        actions->button(QDialogButtonBox::Save)->setText("Apply");
        actions->button(QDialogButtonBox::Save)->setProperty("role", "primary");
        arrayLayout->addWidget(actions, 0, Qt::AlignRight | Qt::AlignAbsolute);
        connect(actions, &QDialogButtonBox::rejected, &arrayDialog, &QDialog::reject);
        connect(actions, &QDialogButtonBox::accepted, &arrayDialog, [&]()
        {
            const QString value = input->toPlainText();
            const auto *metadata = mavlink_get_msg_entry(selectedInfo->msgid);
            Mav::Bytes probe(metadata->max_msg_len, 0);
            std::string reason;
            if (!Mav::setField(probe, field, value.toUtf8().toStdString(), reason))
            {
                arrayError->setText(QString::fromStdString(reason));
                arrayError->show();
                input->setFocus();
                return;
            }
            QString compact = value;
            if (field.type != MAVLINK_TYPE_CHAR)
            {
                QStringList parts = value.split(',');
                for (auto &part : parts)
                {
                    part = part.trimmed();
                }
                compact = parts.join(", ");
            }
            fields->item(row, 2)->setText(compact);
            arrayDialog.accept();
        });
        arrayDialog.exec();
    });
    connect(catalogList, &QListWidget::currentItemChanged, &dialog, [&](QListWidgetItem *item)
    {
        commitEditor();
        submissionAttempted = false;
        if (selectedInfo)
        {
            QStringList values;
            for (int row = 0; row < fields->rowCount(); ++row)
            {
                values.append(fields->item(row, 2)->text());
            }
            drafts[selectedInfo->msgid] = values;
        }
        loadingFields = true;
        selectedInfo = item ? mavlink_get_message_info_by_id(item->data(Qt::UserRole).toUInt()) : nullptr;
        fields->setRowCount(0);
        error->clear();
        error->hide();
        hint->hide();
        arrayButton->hide();
        parameterSearch->clear();
        parameterSearch->setVisible(selectedInfo && selectedInfo->num_fields > 12);
        messageTitle->setText(selectedInfo ? QString("[%1] %2").arg(selectedInfo->msgid).arg(selectedInfo->name) : QString());
        if (!selectedInfo)
        {
            payloadSizeBadge->hide();
            loadingFields = false;
            return;
        }
        const auto *metadata = mavlink_get_msg_entry(selectedInfo->msgid);
        const uint8_t payloadSize = metadata ? metadata->max_msg_len : 0;
        payloadSizeBadge->setText(QString("%1 %2").arg(payloadSize).arg(payloadSize == 1 ? "Byte" : "Bytes"));
        payloadSizeBadge->setStyleSheet(isDark ?
            "color: #94a3b8; font-size: 11px; font-weight: 600; padding: 2px 10px; background: #1e293b; border: 1px solid #334155; border-radius: 6px;" :
            "color: #475569; font-size: 11px; font-weight: 600; padding: 2px 10px; background: #f1f5f9; border: 1px solid #cbd5e1; border-radius: 6px;");
        payloadSizeBadge->setToolTip(QString("Defined MAVLink schema payload length: %1 bytes").arg(payloadSize));
        payloadSizeBadge->show();
        mavlink_message_t source{};
        source.msgid = selectedInfo->msgid;
        if (edit && source.msgid == entry.id)
        {
            source.len = uint8_t(entry.payload.size());
            std::copy(entry.payload.begin(), entry.payload.end(), reinterpret_cast<uint8_t *>(_MAV_PAYLOAD_NON_CONST(&source)));
        }
        fields->setRowCount(int(selectedInfo->num_fields));
        for (unsigned i = 0; i < selectedInfo->num_fields; ++i)
        {
            const auto &field = selectedInfo->fields[i];
            cell(fields, int(i), 0, field.name);
            QString type = QString::fromStdString(Mav::typeName(field.type));
            if (field.array_length)
            {
                type += QString("[%1]").arg(field.array_length);
            }
            cell(fields, int(i), 1, type);
            QFont nameFont = cachedFixedFont();
            nameFont.setPixelSize(13);
            fields->item(int(i), 0)->setFont(nameFont);
            nameFont.setPixelSize(12);
            fields->item(int(i), 1)->setFont(nameFont);
            const auto draft = drafts.find(selectedInfo->msgid);
            const auto enumEntries = Mav::fieldEnumEntries(selectedInfo->msgid, field.name);
            QString initialValue;
            if (draft != drafts.end())
            {
                initialValue = draft->second.value(int(i));
            }
            else if (edit && selectedInfo->msgid == entry.id)
            {
                initialValue = QString::fromStdString(Mav::readField(source, field));
                if (!enumEntries.empty())
                {
                    bool ok = false;
                    const qulonglong num = initialValue.toULongLong(&ok, 0);
                    if (ok)
                    {
                        const std::string name = Mav::lookupEnumName(selectedInfo->msgid, field.name, num);
                        if (!name.empty())
                        {
                            initialValue = QString::fromLatin1(name);
                        }
                    }
                }
            }
            else
            {
                initialValue = QString();
            }
            cell(fields, int(i), 2, initialValue);
            if (!enumEntries.empty())
            {
                QStringList suggestions;
                QVariantList tooltips;
                suggestions.reserve(int(enumEntries.size()));
                tooltips.reserve(int(enumEntries.size()));
                for (const auto &e : enumEntries)
                {
                    suggestions << QString::fromLatin1(e.name);
                    tooltips << QString::number(e.value);
                }
                fields->item(int(i), 2)->setData(Qt::UserRole + 7, suggestions);
                fields->item(int(i), 2)->setData(Qt::UserRole + 8, tooltips);
            }
            if (selectedInfo->msgid == MAVLINK_MSG_ID_HEARTBEAT && qstrcmp(field.name, "mavlink_version") == 0)
            {
                fields->item(int(i), 2)->setText("3");
                fields->item(int(i), 2)->setFlags(fields->item(int(i), 2)->flags() & ~Qt::ItemIsEditable);
                fields->item(int(i), 2)->setToolTip("Protocol constant supplied by the official HEARTBEAT packer");
            }
            for (int column = 0; column < 2; ++column)
            {
                fields->item(int(i), column)->setFlags(fields->item(int(i), column)->flags() & ~Qt::ItemIsEditable);
            }
        }
        loadingFields = false;
        for (int row = 0; row < fields->rowCount(); ++row)
        {
            validateField(row);
        }
        filterParameters();
        updateLoadEstimate();
    });
    connect(search, &QLineEdit::textChanged, &dialog, [=](const QString &query)
    {
        const QSignalBlocker blocker(catalogList);
        auto *current = catalogList->currentItem();
        const QString needle = query.trimmed();
        int count = 0;
        for (int i = 0; i < catalogList->count(); ++i)
        {
            auto *item = catalogList->item(i);
            const QString name = item->data(Qt::UserRole + 3).toString();
            const QString id = QString::number(item->data(Qt::UserRole).toUInt());
            const bool visible = item->text().contains(needle, Qt::CaseInsensitive);
            const bool exact = id == needle || name.compare(needle, Qt::CaseInsensitive) == 0;
            const bool prefix = id.startsWith(needle) || name.startsWith(needle, Qt::CaseInsensitive);
            item->setData(Qt::UserRole + 4, needle.isEmpty() ? 0 : exact ? 0 : prefix ? 1 : 2);
            item->setHidden(!visible);
            count += visible;
        }
        catalogList->sortItems();
        catalogList->setCurrentItem(current);
        catalogList->scrollToTop();
        resultCount->setText(QString::number(count) + " results");
        noMatches->setVisible(count == 0);
        catalogList->setVisible(count != 0);
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]()
    {
        // Commit an active table editor before reading its value.
        commitEditor();

        // Hold Ctrl while clicking to automatically populate remaining empty fields with 0
        if (QApplication::keyboardModifiers() & Qt::ControlModifier)
        {
            for (int row = 0; row < fields->rowCount(); ++row)
            {
                auto *item = fields->item(row, 2);
                if (item && (item->flags() & Qt::ItemIsEditable) && item->text().trimmed().isEmpty())
                {
                    QString zeroValue = "0";
                    if (selectedInfo && row < int(selectedInfo->num_fields))
                    {
                        const auto &field = selectedInfo->fields[row];
                        if (field.array_length > 1 && field.type != MAVLINK_TYPE_CHAR)
                        {
                            QStringList zeros;
                            for (unsigned n = 0; n < field.array_length; ++n)
                            {
                                zeros.append("0");
                            }
                            zeroValue = zeros.join(", ");
                        }
                    }
                    item->setText(zeroValue);
                }
            }
        }

        submissionAttempted = true;
        buttons->setFocus();
        error->clear();
        error->hide();
        for (int row = 0; row < fields->rowCount(); ++row)
        {
            validateField(row);
        }
        if (!selectedInfo)
        {
            error->setText("Select a message");
            error->show();
            return;
        }
        QString compText = compid->currentText().trimmed();
        const int compParen = compText.indexOf('(');
        if (compParen > 0)
        {
            compText = compText.left(compParen).trimmed();
        }
        uint64_t resolvedCompId = 0;
        bool compValid = false;
        const auto symVal = Mav::lookupEnumValue(compText.toStdString());
        if (symVal.has_value() && *symVal <= 255)
        {
            resolvedCompId = *symVal;
            compValid = true;
        }
        else
        {
            bool ok = false;
            const uint64_t num = compText.toULongLong(&ok, 0);
            if (ok && num <= 255)
            {
                resolvedCompId = num;
                compValid = true;
            }
        }
        if (!compValid)
        {
            error->setText("Component ID must be between 0 and 255 or a valid MAV_COMPONENT enum");
            error->show();
            compid->setFocus();
            return;
        }

        const auto *metadata = mavlink_get_msg_entry(selectedInfo->msgid);
        entry.id = selectedInfo->msgid;
        entry.payload.assign(metadata->max_msg_len, 0);
        entry.sysid = uint8_t(sysid->value());
        entry.compid = uint8_t(resolvedCompId);
        entry.version1 = version->currentIndex() == 1;
        entry.hz = rate->value();
        for (unsigned i = 0; i < selectedInfo->num_fields; ++i)
        {
            std::string reason;
            if (!Mav::setField(entry.payload, selectedInfo->fields[i],
                              fields->item(int(i), 2)->text().toUtf8().toStdString(), reason))
            {
                const QString val = fields->item(int(i), 2)->text().trimmed();
                if (val.isEmpty())
                {
                    const auto enumEntries = Mav::fieldEnumEntries(selectedInfo->msgid, selectedInfo->fields[i].name);
                    reason = !enumEntries.empty() ?
                        "Value required: please select or enter an enum name" :
                        "Value required: please enter a value";
                }
                error->setText(QString("%1: %2").arg(selectedInfo->fields[i].name, QString::fromStdString(reason)));
                error->show();
                parameterSearch->clear();
                filterParameters();
                validateField(int(i));
                fields->setCurrentCell(int(i), 2);
                fields->scrollToItem(fields->item(int(i), 2));
                fields->setFocus();
                return;
            }
        }
        if (!Mav::validTx(entry))
        {
            error->setText("MAVLink 1 supports message IDs 0–255 only");
            error->show();
            version->setFocus();
            return;
        }
        dialog.accept();
    });
    int initial = 0;
    for (int i = 0; edit && i < catalogList->count(); ++i)
    {
        if (catalogList->item(i)->data(Qt::UserRole).toUInt() == entry.id)
        {
            initial = i;
            break;
        }
    }
    catalogList->setCurrentRow(initial);
    for (auto *button : dialog.findChildren<QPushButton *>())
    {
        button->setAutoDefault(false);
        button->setDefault(false);
    }
    dialog.installEventFilter(new MessageDialogKeyFilter(saveButton, commitEditor, &dialog));
    if (dialog.exec() == QDialog::Accepted)
    {
        QMetaObject::invokeMethod(worker, "saveEntry", Qt::QueuedConnection, Q_ARG(Mav::TxEntry, entry));
        if (!edit && connected)
        {
            const bool enforceSingle = !singleActiveStreamBox || singleActiveStreamBox->isChecked();
            bool hasActiveDuplicate = false;
            if (enforceSingle && latest)
            {
                for (const auto &item : latest->tx)
                {
                    if (item.entry.active &&
                        item.entry.sysid == entry.sysid &&
                        item.entry.compid == entry.compid &&
                        item.entry.id == entry.id)
                    {
                        hasActiveDuplicate = true;
                        break;
                    }
                }
            }
            if (!hasActiveDuplicate)
            {
                QMetaObject::invokeMethod(worker, "setActive", Qt::QueuedConnection,
                                          Q_ARG(quint64, entry.key), Q_ARG(bool, true));
            }
        }
    }
}

