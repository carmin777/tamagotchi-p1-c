package org.tamap1.game;

import org.libsdl.app.SDLActivity;

/** Runs the native rewrite (libmain.so) on top of SDL. The ROM was already copied by PickActivity. */
public class TamaActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }
}
