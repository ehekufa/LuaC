#!/usr/bin/env bash
set -Eeuo pipefail
log=/tmp/luac-apk-test.log
exec > >(tee "$log") 2>&1
report_failure() {
  status=$?
  trap - ERR
  echo "Device APK test failed at line $1 (exit $status)"
  adb logcat -d -t 2500 2>/dev/null | grep -E 'AndroidRuntime|FATAL EXCEPTION|Caused by:' | tail -30 || true
  message=$(grep -E -i -A 6 'failed|exception|error|caused by|invalid|tests? (passed|failed)|^ok |^success|^verifies|instrumentation_' "$log" | tail -c 3000 | tr '\r\n' '  ' | sed 's/%/%25/g' || true)
  printf '::error file=android/tests/run_apk_device.sh::%s\n' "$message"
  exit "$status"
}
trap 'report_failure "$LINENO"' ERR

gradle --no-daemon :app:assembleDebug :app:assembleDebugAndroidTest
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb install -r app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk
adb shell am instrument -w -r com.luac.studio.test/androidx.test.runner.AndroidJUnitRunner | tee /tmp/luac-instrument.log
grep -q 'OK (2 tests)' /tmp/luac-instrument.log
adb exec-out run-as com.luac.studio cat files/device-project.apk > /tmp/luac-project-test.apk
"$ANDROID_HOME/build-tools/35.0.0/apksigner" verify --verbose /tmp/luac-project-test.apk
"$ANDROID_HOME/build-tools/35.0.0/zipalign" -c 4 /tmp/luac-project-test.apk
adb exec-out run-as com.luac.studio cat files/device-project-second.apk > /tmp/luac-project-second.apk
"$ANDROID_HOME/build-tools/35.0.0/apksigner" verify --verbose /tmp/luac-project-second.apk
AAPT="$ANDROID_HOME/build-tools/35.0.0/aapt"
first=$($AAPT dump badging /tmp/luac-project-test.apk | sed -n "s/^package: name='\([^']*\)'.*/\1/p")
second=$($AAPT dump badging /tmp/luac-project-second.apk | sed -n "s/^package: name='\([^']*\)'.*/\1/p")
[ -n "$first" ] && [ -n "$second" ] && [ "$first" != "$second" ]
adb install -r /tmp/luac-project-test.apk
adb install -r /tmp/luac-project-second.apk
adb shell am start -n "$first/com.luac.studio.MainActivity"
sleep 10
adb shell uiautomator dump /sdcard/luac-window.xml
adb shell cat /sdcard/luac-window.xml | grep 'PROJECT APK DEVICE OK'
adb shell settings put system accelerometer_rotation 0
adb shell settings put system user_rotation 1
sleep 5
adb shell uiautomator dump /sdcard/luac-window.xml
adb shell cat /sdcard/luac-window.xml | grep 'PROJECT APK DEVICE OK'
adb shell settings put system user_rotation 0
adb shell am start -n "$second/com.luac.studio.MainActivity"
sleep 10
adb shell uiautomator dump /sdcard/luac-window.xml
adb shell cat /sdcard/luac-window.xml | grep 'SECOND APK DEVICE OK'
