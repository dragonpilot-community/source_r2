#include "selfdrive/ui/soundd/sound.h"

#include <cmath>

#include <QAudio>
#include <QAudioDeviceInfo>
#include <QDebug>

#include "cereal/messaging/messaging.h"
#include "common/util.h"
#include "common/params.h"

// TODO: detect when we can't play sounds
// TODO: detect when we can't display the UI

Sound::Sound(QObject *parent) : sm({"controlsState", "microphone", "carState"}) {
  qInfo() << "default audio device: " << QAudioDeviceInfo::defaultOutputDevice().deviceName();

  dp_device_audible_alert_mode = std::atoi(Params().get("dp_device_audible_alert_mode").c_str());
  for (auto &[alert, fn, loops] : sound_list) {
    QSoundEffect *s = new QSoundEffect(this);
    QObject::connect(s, &QSoundEffect::statusChanged, [=]() {
      assert(s->status() != QSoundEffect::Error);
    });
    s->setSource(QUrl::fromLocalFile("../../assets/sounds/" + fn));
    sounds[alert] = {s, loops};
  }

  QTimer *timer = new QTimer(this);
  QObject::connect(timer, &QTimer::timeout, this, &Sound::update);
  timer->start(1000 / UI_FREQ);
}

void Sound::update() {
  sm.update(0);

  #ifdef QCOM2
  // scale volume using ambient noise level
  if (sm.updated("microphone")) {
    float volume = util::map_val(sm["microphone"].getMicrophone().getFilteredSoundPressureWeightedDb(), 30.f, 60.f, 0.f, 1.f);
    volume = QAudio::convertVolume(volume, QAudio::LogarithmicVolumeScale, QAudio::LinearVolumeScale);
    // set volume on changes
    if (std::exchange(current_volume, std::nearbyint(volume * 10)) != current_volume) {
      Hardware::set_volume(volume);
    }
  }
  #else
  if (sm.updated("carState")) {
    float volume = util::map_val(sm["carState"].getCarState().getVEgo(), 11.f, 20.f, 0.f, 1.0f);
    volume = QAudio::convertVolume(volume, QAudio::LogarithmicVolumeScale, QAudio::LinearVolumeScale);
    volume = util::map_val(volume, 0.f, 1.f, Hardware::MIN_VOLUME, Hardware::MAX_VOLUME);
    for (auto &[s, loops] : sounds) {
      s->setVolume(std::round(100 * volume) / 100);
    }
  }
  #endif
  setAlert(Alert::get(sm, 0));
}

void Sound::setAlert(const Alert &alert) {
  if (!current_alert.equal(alert)) {
    current_alert = alert;
    // stop sounds
    for (auto &[s, loops] : sounds) {
      // Only stop repeating sounds
      if (s->loopsRemaining() > 1 || s->loopsRemaining() == QSoundEffect::Infinite) {
        s->stop();
      }
    }

    // play sound
    if (shouldPlaySound(alert)) {
      auto &[s, loops] = sounds[alert.sound];
      s->setLoopCount(loops);
      s->play();
    }
  }
}

bool Sound::shouldPlaySound(const Alert &alert) {
//    tr("Standard"), tr("Warning/Alert"), tr("Off")
  if (dp_device_audible_alert_mode > 0) {
    // off - Does not emit any sound at all.
    if (dp_device_audible_alert_mode == 2) {
      return false;
    // Warning - Only emits sound when there is a warning.
    } else if (dp_device_audible_alert_mode == 1) {
      return (alert.sound == AudibleAlert::WARNING_IMMEDIATE || alert.sound == AudibleAlert::PROMPT_REPEAT || alert.sound == AudibleAlert::PROMPT_DISTRACTED);
    } else {
      return alert.sound != AudibleAlert::NONE;
    }
  } else {
    return alert.sound != AudibleAlert::NONE;
  }
}
