package com.grimace.shiningemulatorandroid;

import android.app.NativeActivity;
import android.app.Activity;

import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.content.ContentResolver;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;
import android.util.Log;

import java.io.*;

public class MainActivity extends NativeActivity {

    private static MainActivity localInstance;

    private static final String TAG = MainActivity.class.getName();
    private static final int RQ_CODE_CHOOSE_FILE = 0xface;
    static {
        System.loadLibrary("android-app");
        localInstance = null;
    }

    public static String getAppDir() {
        if (localInstance == null) {
            return null;
        }

        File appFilePath = localInstance.getFilesDir();
        return appFilePath.getAbsolutePath();
    }

    public static void launchFilePicker() {
        if (localInstance != null) {
            new Handler(Looper.getMainLooper()).post(new Runnable() {
                @Override public void run() {
                    localInstance.openPicker();
                }
            });
        }
    }

    private void openPicker() {
        Intent filePickerIntent = new Intent(Intent.ACTION_GET_CONTENT);
        filePickerIntent.setType("*/*");
        filePickerIntent.addCategory(Intent.CATEGORY_OPENABLE);
        filePickerIntent.putExtra(Intent.EXTRA_LOCAL_ONLY, true);
        startActivityForResult(filePickerIntent, RQ_CODE_CHOOSE_FILE);
    }

    private String[] fileTypes = null;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        localInstance = this;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == RQ_CODE_CHOOSE_FILE && resultCode == Activity.RESULT_OK && data != null) {
            Uri uri = data.getData();
            if (uri != null) {
                String scheme = uri.getScheme();
                uri.getEncodedPath();
                if (scheme != null && (scheme.equals(ContentResolver.SCHEME_CONTENT) || scheme.equals(ContentResolver.SCHEME_FILE))) {
                    ContentResolver resolver = getContentResolver();
                    if (resolver != null) try {
                        String fileName = null;
                        Cursor rows = resolver.query(uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null);
                        if (rows != null) try {
                            if (rows.moveToFirst()) {
                                fileName = rows.getString(0);
                            }
                        }
                        finally {
                            rows.close();
                        }
                        String useName = fileName != null ? fileName : "tempFile";
                        byte[] buff = new byte[32768];
                        File copyFile = new File(getCacheDir(), useName);
                        InputStream inStream = resolver.openInputStream(uri);
                        if (inStream != null) {
                            try (OutputStream outStream = new FileOutputStream(copyFile)) {
                                int bytesRead;
                                while ((bytesRead = inStream.read(buff)) > 0) {
                                    outStream.write(buff, 0, bytesRead);
                                }
                            }
                            filePicked(copyFile.getAbsolutePath());
                        }
                    }
                    catch (IOException err) {
                        Log.d(TAG, err.toString());
                    }
                    catch (Exception err) {
                        Log.e(TAG, err.toString());
                    }
                }
            }
        }
    }

    public native void filePicked(String fileName);
}
