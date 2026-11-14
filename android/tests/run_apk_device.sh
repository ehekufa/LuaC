#!/usr/bin/env bash
set -Eeuo pipefail
log=/tmp/luac-apk-test.log
exec > >(tee "$log") 2>&1
report_failure() {
  status=$?
  trap - ERR
  echo "Device APK test failed at line $1 (exit $status)"
  for report in app/build/outputs/androidTest-results/connected/debug/*.xml; do
    [ -f "$report" ] && cat "$report" || true
  done
  adb logcat -d -t 2500 2>/dev/null | grep -E 'AndroidRuntime|FATAL EXCEPTION|Caused by:|luac|LuaC' | tail -100 || true
  message=$(tail -c 3000 "$log" | tr '\r\n' '  ' | sed 's/%/%25/g')
  printf '::error file=android/tests/run_apk_device.sh::%s\n' "$message"
  exit "$status"
}
trap 'report_failure "$LINENO"' ERR

gradle --no-daemon :app:connectedDebugAndroidTest
adb exec-out run-as com.luac.studio cat files/device-project.apk > /tmp/luac-project-test.apk
"$ANDROID_HOME/build-tools/35.0.0/apksigner" verify --verbose /tmp/luac-project-test.apk
"$ANDROID_HOME/build-tools/35.0.0/zipalign" -c 4 /tmp/luac-project-test.apk
adb install -r /tmp/luac-project-test.apk
adb shell am start -n com.luac.projectrunner/com.luac.studio.MainActivity
sleep 10
adb shell uiautomator dump /sdcard/luac-window.xml
adb shell cat /sdcard/luac-window.xml | grep 'PROJECT APK DEVICE OK'
