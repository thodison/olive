#ifndef PROJECTFILEMANAGERBASE_H
#define PROJECTFILEMANAGERBASE_H

#include <QObject>

#include "project.h"

class ProjectFileManagerBase : public QObject
{
  Q_OBJECT
public:
  ProjectFileManagerBase();

public slots:
  /**
   * @brief Start the save process
   *
   * It's recommended to invoke this through Qt signals/slots/QueuedConnection after moving this object to a separate
   * thread.
   */
  virtual void Start() = 0;

  /**
   * @brief Cancel the current save
   *
   * Always connect to this with a DirectConnection. Otherwise, it'll be queued AFTER the save function is already
   * complete.
   */
  void Cancel();

signals:
  void ProgressChanged(int);

  void Finished();

private:
  QAtomicInt cancelled_;

};

#endif // PROJECTFILEMANAGERBASE_H
