#include "DecompilerWidget.h"
#include "ui_DecompilerWidget.h"

#include "common/Configuration.h"
#include "common/Helpers.h"
#include "common/TempConfig.h"
#include "common/SelectionHighlight.h"
#include "common/Decompiler.h"

#include <QTextEdit>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QObject>
#include <QTextBlockUserData>

/**
 * Represents a single line of decompiled code as part of the displayed text,
 * including the position inside the QTextDocument
 */
struct DecompiledCodeTextLine
{
    /**
     * position inside the QTextDocument
     */
    int position;

    DecompiledCode::Line line;

    DecompiledCodeTextLine(int position, const DecompiledCode::Line &line)
    {
        this->position = position;
        this->line = line;
    }
};


DecompilerWidget::DecompilerWidget(MainWindow *main, QAction *action) :
    MemoryDockWidget(MemoryWidgetType::Decompiler, main, action),
    ui(new Ui::DecompilerWidget)
{
    ui->setupUi(this);

    syntaxHighlighter = Config()->createSyntaxHighlighter(ui->textEdit->document());

    setupFonts();
    colorsUpdatedSlot();

    connect(Config(), SIGNAL(fontsUpdated()), this, SLOT(fontsUpdated()));
    connect(Config(), SIGNAL(colorsUpdated()), this, SLOT(colorsUpdatedSlot()));

    // TODO Use RefreshDeferrer and remove the refresh button
    connect(ui->refreshButton, &QAbstractButton::clicked, this, [this]() {
        doRefresh(Core()->getOffset());
    });

    auto decompilers = Core()->getDecompilers();
    selectedDecompilerId = Config()->getSelectedDecompiler();
    for (auto dec : decompilers) {
        ui->decompilerComboBox->addItem(dec->getName(), dec->getId());
        if (dec->getId() == selectedDecompilerId) {
            ui->decompilerComboBox->setCurrentIndex(ui->decompilerComboBox->count() - 1);
        }
    }

    if(decompilers.size() <= 1) {
        ui->decompilerComboBox->setEnabled(false);
        if (decompilers.isEmpty()) {
            ui->textEdit->setPlainText(tr("No Decompiler available."));
        }
    }

    setWindowTitle(getWindowTitle());
    connect(ui->decompilerComboBox, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &DecompilerWidget::decompilerSelected);
    connectCursorPositionChanged(false);
    connect(Core(), &CutterCore::seekChanged, this, &DecompilerWidget::seekChanged);

    doRefresh(RVA_INVALID);
}

DecompilerWidget::~DecompilerWidget() = default;


void DecompilerWidget::doRefresh(RVA addr)
{
    if (ui->decompilerComboBox->currentIndex() < 0) {
        return;
    }

    Decompiler *dec = Core()->getDecompilerById(ui->decompilerComboBox->currentData().toString());
    if (!dec) {
        return;
    }

    if (addr == RVA_INVALID) {
        ui->textEdit->setPlainText(tr("Click Refresh to decompile from current offset."));
        return;
    }

    DecompiledCode decompiledCode = dec->decompileAt(addr);

    textLines = {};
    textLines.reserve(decompiledCode.lines.size());

    if (decompiledCode.lines.isEmpty()) {
        ui->textEdit->setPlainText(tr("Cannot decompile at") + " " + RAddressString(
                                  addr) + " " + tr("(Not a function?)"));
        return;
    } else {
        connectCursorPositionChanged(true);
        ui->textEdit->document()->clear();
        QTextCursor cursor(ui->textEdit->document());
        for (const DecompiledCode::Line &line : decompiledCode.lines) {
            textLines.append(DecompiledCodeTextLine(cursor.position(), line));
            // Can't use cursor.block()->setUserData() here, because the Syntax Highlighter will mess it up.
            cursor.insertText(line.str + "\n");
        }
        connectCursorPositionChanged(false);
        seekChanged();
    }
}

void DecompilerWidget::refreshDecompiler()
{
    doRefresh(Core()->getOffset());
}

void DecompilerWidget::decompilerSelected()
{
    Configuration().setSelectedDecompiler(ui->decompilerComboBox->currentData().toString());
}

void DecompilerWidget::connectCursorPositionChanged(bool disconnect)
{
    if (disconnect) {
        QObject::disconnect(ui->textEdit, &QPlainTextEdit::cursorPositionChanged, this, &DecompilerWidget::cursorPositionChanged);
    } else {
        connect(ui->textEdit, &QPlainTextEdit::cursorPositionChanged, this, &DecompilerWidget::cursorPositionChanged);
    }
}

void DecompilerWidget::cursorPositionChanged()
{
    RVA offset = getOffsetAtLine(ui->textEdit->textCursor());
    if (offset != RVA_INVALID && offset != Core()->getOffset()) {
        seekFromCursor = true;
        Core()->seek(offset);
        seekFromCursor = false;
    }
    updateSelection();
}

void DecompilerWidget::seekChanged()
{
    if (seekFromCursor) {
        return;
    }
    updateCursorPosition();
}

void DecompilerWidget::updateCursorPosition()
{
    RVA offset = Core()->getOffset();
    connectCursorPositionChanged(true);

    auto it = findLineByOffset(offset);
    if (it != textLines.end()) {
        // move back if the offset is identical (so we don't land on closing braces for example)
        while (it != textLines.begin()) {
            auto prev = it - 1;
            if (prev->line.addr != it->line.addr) {
                break;
            }
            it = prev;
        }
        QTextCursor cursor = ui->textEdit->textCursor();
        cursor.setPosition((*it).position);
        ui->textEdit->setTextCursor(cursor);
        updateSelection();
    }

    connectCursorPositionChanged(false);
}

QList<DecompiledCodeTextLine>::iterator DecompilerWidget::findLine(int position)
{
    return std::upper_bound(textLines.begin(), textLines.end(), position,
                            [](int pos, const DecompiledCodeTextLine &line) {
                                return pos < line.position;
                            });
}

QList<DecompiledCodeTextLine>::iterator DecompilerWidget::findLineByOffset(RVA offset)
{
    auto it = textLines.begin();
    auto candidate = it;
    for (; it != textLines.end(); it++) {
        RVA lineOffset = it->line.addr;
        if (lineOffset != RVA_INVALID && lineOffset > offset) {
            break;
        }
        if (candidate->line.addr == RVA_INVALID || (lineOffset != RVA_INVALID && lineOffset > candidate->line.addr)) {
            candidate = it;
        }
    }
    return candidate;
}

RVA DecompilerWidget::getOffsetAtLine(const QTextCursor &tc)
{
    auto it = findLine(tc.position());
    if (it == textLines.begin()) {
        return RVA_INVALID;
    }
    it--;
    return (*it).line.addr;
}

void DecompilerWidget::setupFonts()
{
    QFont font = Config()->getFont();
    ui->textEdit->setFont(font);
}

void DecompilerWidget::updateSelection()
{
    QList<QTextEdit::ExtraSelection> extraSelections;

    // Highlight the current line
    auto cursor = ui->textEdit->textCursor();

    RVA cursorOffset = getOffsetAtLine(cursor);
    if (cursorOffset != RVA_INVALID) {
        for (auto it = findLineByOffset(cursorOffset);
                it != textLines.end() && it->line.addr != RVA_INVALID && it->line.addr <= cursorOffset;
                it++) {
            auto lineCursor = cursor;
            lineCursor.setPosition(it->position);
            extraSelections.append(createLineHighlightSelection(lineCursor));
        }
    } else {
        // if the cursor position has no valid offset, just highlight the line
        extraSelections.append(createLineHighlightSelection(cursor));
    }

    // Highlight all the words in the document same as the current one
    cursor.select(QTextCursor::WordUnderCursor);
    QString searchString = cursor.selectedText();
    extraSelections.append(createSameWordsSelections(ui->textEdit, searchString));

    ui->textEdit->setExtraSelections(extraSelections);
}

QString DecompilerWidget::getWindowTitle() const
{
    QString title = tr("Decompiler");
    if (!selectedDecompilerId.isNull()) {
        title += QString(" (%1)").arg(selectedDecompilerId);
    }

    return title;
}

void DecompilerWidget::fontsUpdated()
{
    setupFonts();
}

void DecompilerWidget::colorsUpdatedSlot()
{
}
