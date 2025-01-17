#pragma once

#include <EditorFramework/ui_EnginePluginConfigDlg.h>
#include <Foundation/Basics.h>
#include <QDialog>

class ezQtEnginePluginConfigDlg : public QDialog, public Ui_ezQtEnginePluginConfigDlg
{
public:
  Q_OBJECT

public:
  ezQtEnginePluginConfigDlg(QWidget* parent);


private Q_SLOTS:
  void on_ButtonOK_clicked();
  void on_ButtonCancel_clicked();

private:
  void FillPluginList();
};

