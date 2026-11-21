package app.loam;

import android.app.Activity;
import android.os.Bundle;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

/** Thin shell: the whole app is the bundled web viewer (assets/index.html). The world lives on your server. */
public class MainActivity extends Activity {
    private WebView web;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        web = new WebView(this);
        web.setBackgroundColor(0xFF05040A);
        setContentView(web);
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);           // remembers server address + key on this device only
        s.setAllowContentAccess(false);
        web.setWebViewClient(new WebViewClient());   // keeps every navigation inside the app
        web.loadUrl("file:///android_asset/index.html");
        web.requestFocus();
    }

    @Override
    public void onBackPressed() {
        moveTaskToBack(true);                    // keep the app (and its state) alive
    }
}
