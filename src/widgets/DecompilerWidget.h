#ifndef DecompilerWidget_H
#define DecompilerWidget_H

#include <memory>

#include "core/Cutter.h"
#include "MemoryDockWidget.h"

namespace Ui {
class DecompilerWidget;
}

class QTextEdit;
class QSyntaxHighlighter;
class QTextCursor;
struct DecompiledCodeTextLine;

class DecompilerWidget : public MemoryDockWidget
{
    Q_OBJECT

public:
    explicit DecompilerWidget(MainWindow *main, QAction *action = nullptr);
    ~DecompilerWidget();

private slots:
    void fontsUpdated();
    void colorsUpdatedSlot();
    void refreshDecompiler();
    void decompilerSelected();
    void cursorPositionChanged();
    void seekChanged();

private:
    std::unique_ptr<Ui::DecompilerWidget> ui;

    QSyntaxHighlighter *syntaxHighlighter;

    /**
     * Index of all lines that are currently displayed, ordered by the position in the text
     */
    QList<DecompiledCodeTextLine> textLines;

    bool seekFromCursor = false;
    QString selectedDecompilerId;

    void doRefresh(RVA addr);
    void setupFonts();
    void updateSelection();
    void connectCursorPositionChanged(bool disconnect);
    void updateCursorPosition();

    /**
     * @return Iterator to the first line that is after position or last if not found.
     */
    QList<DecompiledCodeTextLine>::iterator findLine(int position);

    /**
     * @return Iterator to the first line that is considered to contain offset
     */
    QList<DecompiledCodeTextLine>::iterator findLineByOffset(RVA offset);
    RVA getOffsetAtLine(const QTextCursor &tc);

    QString getWindowTitle() const override;
};

#endif // DecompilerWidget_H
