#ifndef MAEMODEPLOYSTEPWIDGET_H
#define MAEMODEPLOYSTEPWIDGET_H

#include <projectexplorer/buildstep.h>

QT_BEGIN_NAMESPACE
namespace Ui {
    class MaemoDeployStepWidget;
}
QT_END_NAMESPACE

namespace Qt4ProjectManager {
namespace Internal {

class MaemoDeployStepWidget : public ProjectExplorer::BuildStepConfigWidget
{
    Q_OBJECT

public:
    MaemoDeployStepWidget();
    ~MaemoDeployStepWidget();

private:
    virtual void init();
    virtual QString summaryText() const;
    virtual QString displayName() const;

    Ui::MaemoDeployStepWidget *ui;
};

} // namespace Internal
} // namespace Qt4ProjectManager

#endif // MAEMODEPLOYSTEPWIDGET_H
