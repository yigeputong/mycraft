package com.yigeputong.mycraft;

import org.libsdl.app.SDLActivity;

public class MainActivity extends SDLActivity {

    // 重写 getLibraries 方法，去掉默认的 "SDL3"，只加载我们的主库 "main"
    @Override
    protected String[] getLibraries() {
        return new String[] {
                "main"
        };
    }
}