package org.tamap1.game;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;

/**
 * Tela inicial: pede o dump da ROM (tama.b, 12288 bytes) uma única vez e o copia para o
 * armazenamento interno do app (getFilesDir()/tama.b, onde o código nativo o procura).
 */
public class PickActivity extends Activity {
    private static final int REQ_ROM = 1;
    private static final int ROM_SIZE = 12288;

    private TextView msg;

    private File romFile() {
        return new File(getFilesDir(), "tama.b");
    }

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        if (romFile().length() == ROM_SIZE) {
            startGame();
            return;
        }
        LinearLayout l = new LinearLayout(this);
        l.setOrientation(LinearLayout.VERTICAL);
        l.setGravity(Gravity.CENTER);
        l.setPadding(48, 48, 48, 48);
        TextView t = new TextView(this);
        t.setText("Tamagotchi P1\n\nEscolha o dump da ROM (tama.b, 12288 bytes). "
                + "Ele é copiado só para o armazenamento do app e não sai do aparelho.");
        t.setTextSize(18);
        Button pick = new Button(this);
        pick.setText("Escolher ROM");
        pick.setOnClickListener(v -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("*/*");
            startActivityForResult(i, REQ_ROM);
        });
        msg = new TextView(this);
        l.addView(t);
        l.addView(pick);
        l.addView(msg);
        setContentView(l);
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != REQ_ROM || res != RESULT_OK || data == null) return;
        Uri uri = data.getData();
        File tmp = new File(getFilesDir(), "tama.b.tmp");
        try (InputStream in = getContentResolver().openInputStream(uri);
             FileOutputStream out = new FileOutputStream(tmp)) {
            byte[] buf = new byte[4096];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        } catch (Exception e) {
            msg.setText("Não consegui ler o arquivo: " + e.getMessage());
            return;
        }
        if (tmp.length() != ROM_SIZE) {
            msg.setText("Arquivo com " + tmp.length() + " bytes; o esperado é " + ROM_SIZE + ".");
            tmp.delete();
            return;
        }
        tmp.renameTo(romFile());
        startGame();
    }

    private void startGame() {
        startActivity(new Intent(this, TamaActivity.class));
        finish();
    }
}
