#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

namespace Ui {
class MainWindow;
}

class XmlElement;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private slots:
    void on_loadFile_clicked();
    void on_saveHtml_clicked();

private:
    Ui::MainWindow *ui;
    XmlElement *root_element;
};

#endif // MAINWINDOW_H
