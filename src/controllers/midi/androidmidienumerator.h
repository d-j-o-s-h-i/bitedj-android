#pragma once
#include "controllers/midi/midienumerator.h"
#include "preferences/usersettings.h"

class AndroidMidiEnumerator final : public MidiEnumerator {
  public:
    explicit AndroidMidiEnumerator(UserSettingsPointer config);
    ~AndroidMidiEnumerator() override;
    QList<Controller*> queryDevices() override;
    bool needPolling() override { return true; }
  private:
    UserSettingsPointer m_pConfig;
    QList<Controller*> m_devices;
};
