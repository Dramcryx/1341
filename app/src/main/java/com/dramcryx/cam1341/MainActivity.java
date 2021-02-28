package com.dramcryx.cam1341;


import androidx.appcompat.app.AppCompatActivity;

import android.Manifest;
import android.graphics.ImageFormat;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraMetadata;
import android.media.Image;
import android.os.Bundle;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceControl;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.widget.TextView;


class SurfaceDisplay implements SurfaceHolder.Callback {
    SurfaceView surfaceView;
    Surface surface;
    Device camera;
    long milliseconds;

    SurfaceDisplay(SurfaceView view) {
        this.surfaceView = view;
        SurfaceHolder holder = surfaceView.getHolder();
        holder.setFormat(PixelFormat.RGBA_8888);
        holder.addCallback(this);

        CameraModel.Init();
        milliseconds = System.currentTimeMillis();
    }

    private void logTimeDiff() {
        long time = System.currentTimeMillis();
        Log.w("", Long.toString(time - milliseconds));
        milliseconds = time;
    }

    @Override
    public void surfaceCreated(SurfaceHolder surfaceHolder) {
        // Get all devices in array form
        for (Device device : CameraModel.GetDevices()) {
            if (device.facing() == CameraCharacteristics.LENS_FACING_BACK) {
                camera = device;
            }
        }
        logTimeDiff();
    }

    @Override
    public void surfaceChanged(SurfaceHolder surfaceHolder,
                               int format, int width, int height) {
        // Make a repeating caputre request with surface
        surface = surfaceHolder.getSurface();
        camera.repeat(surface);
        logTimeDiff();
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder surfaceHolder) {
        // No more capture
        if (camera != null)
//            camera.stopCapture();
            camera.stopRepeat();
        logTimeDiff();
    }
}

public class MainActivity extends AppCompatActivity {
    SurfaceDisplay display;

    // Used to load the 'native-lib' library on application startup.
    static {
        System.loadLibrary("native-lib");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        setContentView(R.layout.activity_main);
        display = new SurfaceDisplay(findViewById(R.id.surfaceView));
        // Example of a call to a native method


    }
}