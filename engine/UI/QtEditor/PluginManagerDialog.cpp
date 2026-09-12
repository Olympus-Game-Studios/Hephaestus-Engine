#if HEPHAESTUS_EDITOR_QT_UI
#include "PluginManagerDialog.h"
#include "../../Plugins/PluginSystem.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

namespace Hephaestus::QtEditor {
PluginManagerDialog::PluginManagerDialog(Plugins::PluginSystem& system,QWidget*parent):QDialog(parent),system_(system){
 setWindowTitle(tr("Plugins"));resize(880,540);auto*l=new QVBoxLayout(this);
 table_=new QTableWidget(this);table_->setColumnCount(5);table_->setHorizontalHeaderLabels({tr("Enabled"),tr("Name"),tr("Version"),tr("Author"),tr("Status")});table_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);l->addWidget(table_,2);
 details_=new QTextEdit(this);details_->setReadOnly(true);l->addWidget(details_,1);auto*b=new QDialogButtonBox(QDialogButtonBox::Close,this);connect(b,&QDialogButtonBox::rejected,this,&QDialog::reject);l->addWidget(b);connect(table_,&QTableWidget::currentCellChanged,this,[this](int r,int,int,int){ShowDetails(r);});Rebuild();
}
void PluginManagerDialog::Rebuild(){const auto&rs=system_.Records();table_->setRowCount(static_cast<int>(rs.size()));for(int row=0;row<table_->rowCount();++row){const auto&r=rs[static_cast<size_t>(row)];auto*enabled=new QCheckBox(table_);enabled->setChecked(r.enabled);enabled->setEnabled(r.status!=Plugins::PluginStatus::Invalid&&r.status!=Plugins::PluginStatus::Duplicate&&r.status!=Plugins::PluginStatus::Incompatible);table_->setCellWidget(row,0,enabled);auto*name=new QTableWidgetItem(QString::fromStdString(r.manifest.name.empty()?r.manifest.manifestPath.filename().string():r.manifest.name));if(!r.manifest.icon.empty())name->setIcon(QIcon(QString::fromStdString((r.manifest.root/r.manifest.icon).string())));table_->setItem(row,1,name);table_->setItem(row,2,new QTableWidgetItem(QString::fromStdString(r.manifest.pluginVersion)));table_->setItem(row,3,new QTableWidgetItem(QString::fromStdString(r.manifest.author)));table_->setItem(row,4,new QTableWidgetItem(QString::fromUtf8(Plugins::PluginSystem::StatusText(r.status))));connect(enabled,&QCheckBox::toggled,this,[this,row](bool value){auto&rs=system_.Records();if(row<0||static_cast<size_t>(row)>=rs.size())return;std::string error;if(!system_.SetEnabled(rs[static_cast<size_t>(row)].manifest.id,value,&error)){QMessageBox::warning(this,tr("Plugin State"),QString::fromStdString(error));Rebuild();return;}table_->item(row,4)->setText(tr("Restart required"));ShowDetails(row);});}if(!rs.empty()){table_->selectRow(0);ShowDetails(0);}}
void PluginManagerDialog::ShowDetails(int row){const auto&rs=system_.Records();if(row<0||static_cast<size_t>(row)>=rs.size()){details_->clear();return;}const auto&r=rs[static_cast<size_t>(row)];details_->setHtml(QString("<b>%1</b><br>%2<br><br><b>ID:</b> %3<br><b>Location:</b> %4<br><b>Manifest:</b> %5<br><b>Compatibility:</b> %6<br><b>Error:</b> %7%8").arg(QString::fromStdString(r.manifest.name).toHtmlEscaped(),QString::fromStdString(r.manifest.description).toHtmlEscaped(),QString::fromStdString(r.manifest.id).toHtmlEscaped(),r.location==Plugins::PluginLocation::Engine?tr("Engine"):tr("Project"),QString::fromStdString(r.manifest.manifestPath.string()).toHtmlEscaped(),QString::fromUtf8(Plugins::PluginSystem::StatusText(r.status)),QString::fromStdString(r.error).toHtmlEscaped(),r.restartPending?tr("<br><b>Restart the editor to apply this change.</b>"):QString()));}
}
#endif
