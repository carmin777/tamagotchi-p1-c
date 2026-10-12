package org.tamap1.game;

import org.libsdl.app.SDLActivity;

/** Roda a reescrita nativa (libmain.so) em cima do SDL. A ROM já foi copiada pela PickActivity. */
public class TamaActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }
}
