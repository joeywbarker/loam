# Loam for Android

A thin WebView shell around `app/web/index.html`. The world lives on your server; the app only shows a window of it
and stores the server address and key on the device (backups are disabled so the key never leaves the phone).

## Getting the APK
Publish a GitHub Release (any tag, e.g. `v0.1.0`): the `android-apk` workflow builds the app and attaches
`loam-<version>.apk` to that release. You can also run the workflow by hand (Actions -> android-apk -> Run workflow)
and download the APK from the run's artifacts. On your phone, open the APK and allow "install unknown apps".

## Updating without uninstalling (optional but recommended)
Without a signing key, every build is signed with a fresh throwaway debug key, and Android will not install a new
build over an old one (you'd uninstall first). Make one stable key once and give it to GitHub:

    keytool -genkeypair -v -keystore loam.jks -alias loam -keyalg RSA -keysize 2048 -validity 10000
    base64 -w0 loam.jks      # paste into repo secret ANDROID_KEYSTORE_B64
    # repo secret ANDROID_KEYSTORE_PASSWORD = the password you chose (use the same for key and store)

Keep `loam.jks` somewhere safe and never commit it. Lose it and you can't update installed copies.

The app only talks to `https://` servers (cleartext is disabled).
