package br.com.tk90x.emulador;

import android.annotation.SuppressLint;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.util.Log;
import android.view.DisplayCutout;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

/**
 * Activity do emulador TK90X: o SDLActivity (que roda o main() de src/main.c numa
 * thread propria) mais:
 *  - seletor de arquivos do aparelho (ACTION_OPEN_DOCUMENT), chamado pelo C via JNI
 *    (src/android_bridge.c, android_open_picker);
 *  - "Abrir com" (ACTION_VIEW) de gerenciadores de arquivos e downloads do navegador;
 *  - tela sempre ligada e modo imersivo reaplicado ao voltar o foco;
 *  - tela cheia e recorte da camera (notch/furo) definidos antes do primeiro layout,
 *    e as faixas cobertas pelo recorte informadas ao C (nativeSafeInsets).
 * Nos dois casos o documento (content://) e copiado para getCacheDir()/picked/<nome>
 * e o caminho comum vai para o C por nativeFilePicked().
 */
public class TK90XActivity extends SDLActivity {
    private static final String TAG = "TK90X";
    private static final int REQ_OPEN = 0x7C90;
    private static final long MAX_FILE = 16L * 1024 * 1024;   /* nenhuma fita chega perto */
    /**
     * Tamanho maximo do nome no cache, em bytes UTF-8: ext4/f2fs aceitam 255 bytes por
     * nome, e a copia temporaria ainda usa "." + nome + ".tmp".
     */
    private static final int MAX_NAME_BYTES = 200;

    /** Extensoes aceitas (as mesmas do navegador interno, osd_supported_file). */
    private static final String[] EXTENSIONS = {
        "tap", "tzx", "sna", "z80", "scr", "bin", "cod"
    };

    /** Uma entrega por vez: as copias para o cache nao se atropelam. */
    private static final Object PICK_LOCK = new Object();

    /**
     * Implementado em libmain.so (src/android_bridge.c). O caminho vai em bytes UTF-8
     * (e nao como String): GetStringUTFChars daria UTF-8 "modificado" do Java, diferente
     * dos bytes reais do nome no disco quando ele tem emoji ou outro caractere fora do
     * BMP. null = cancelado.
     */
    private static native void nativeFilePicked(byte[] pathUtf8);

    private static void filePicked(String path) {
        nativeFilePicked(path == null ? null : path.getBytes(StandardCharsets.UTF_8));
    }

    /**
     * Implementado em libmain.so (src/android_bridge.c): faixas da janela cobertas pelo
     * recorte da camera (DisplayCutout), em pixels. A imagem e os controles de toque
     * ficam fora delas (src/touch.c, touch_set_safe_insets).
     */
    private static native void nativeSafeInsets(int left, int top, int right, int bottom);

    /** Barras do sistema escondidas; voltam com um gesto e somem sozinhas (imersivo). */
    private static final int IMMERSIVE_FLAGS =
            View.SYSTEM_UI_FLAG_FULLSCREEN
            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_LAYOUT_STABLE;

    /** Ultimo recorte informado ao C (esquerda, alto, direita, baixo). */
    private final int[] mInsets = { -1, -1, -1, -1 };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // O SDLActivity.onCreate entrega intent.getData().getPath() ao C como "arquivo
        // solto" (SDL_DROPFILE), o que nao serve para content://. Tira os dados da
        // intent antes e trata o "Abrir com" aqui mesmo, depois de carregar as bibliotecas.
        Intent orig = getIntent();
        Intent view = null;
        if (orig != null && orig.getData() != null) {
            view = orig;
            Intent stripped = new Intent(orig);
            stripped.setData(null);
            setIntent(stripped);
        }
        // A janela ja nasce do tamanho final: tela cheia e ocupando o recorte da camera
        // (tambem no tema, values-v28). Se o tamanho mudasse depois que a superficie do
        // SDL existe, o Android 11 so aplicaria o tamanho novo da SurfaceView junto com
        // um quadro novo da janela-mae, que este app nunca desenha (tudo vai para a
        // SurfaceView): o SurfaceFlinger ficaria recusando os quadros do tamanho novo e
        // a imagem congelaria (docs/ANDROID.md, "Imagem congelada com recorte").
        applyWindowFlags();
        super.onCreate(savedInstanceState);
        if (mBrokenLibraries) return;          /* o SDL ja mostrou o erro */
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        // O SDLActivity.onCreate enfileirou setWindowStyle(false) (barras visiveis,
        // FLAG_FORCE_NOT_FULLSCREEN), que roda depois do onCreate mas antes do primeiro
        // layout. Esta mensagem entra na fila depois daquela e restaura o modo imersivo
        // ainda antes do primeiro layout; o setWindowStyle(true) que vem do C ao criar a
        // janela SDL (SDL_WINDOW_FULLSCREEN_DESKTOP) pede os mesmos sinalizadores, e a
        // janela nao muda de tamanho.
        new Handler(Looper.getMainLooper()).post(this::hideSystemUi);
        watchCutout();
        // So abre o arquivo num lancamento de verdade. Uma Activity recriada (mudanca de
        // configuracao nao tratada, ou processo encerrado e retomado pelos Recentes)
        // recebe de novo a intent VIEW original da tarefa: reabrir aquele arquivo
        // ignoraria o que o usuario abriu depois, e a permissao da URI pode ter expirado.
        boolean fromHistory = orig != null
                && (orig.getFlags() & Intent.FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY) != 0;
        if (view != null && savedInstanceState == null && !fromHistory) handleViewIntent(view);
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        // launchMode singleTask: "Abrir com" com o app ja aberto chega aqui.
        if (intent != null && intent.getData() != null) handleViewIntent(intent);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemUi();
    }

    @Override
    protected void onResume() {
        super.onResume();
        hideSystemUi();
    }

    /**
     * Tela cheia imersiva. So mexe no que estiver diferente: cada setAttributes forca
     * um novo layout da janela, e repetir sem necessidade a cada foco nao ajuda nada.
     */
    private void hideSystemUi() {
        applyWindowFlags();
        View decor = getWindow().getDecorView();
        if (decor.getSystemUiVisibility() != IMMERSIVE_FLAGS)
            decor.setSystemUiVisibility(IMMERSIVE_FLAGS);
    }

    /**
     * FLAG_FULLSCREEN (sem FLAG_FORCE_NOT_FULLSCREEN) e, no Android 9+, a janela ocupando
     * o recorte da camera nas bordas curtas (o lado do recorte em paisagem). Pode ser
     * chamado antes do super.onCreate: so usa os atributos da janela, nao a decor view.
     */
    private void applyWindowFlags() {
        Window w = getWindow();
        final int full = WindowManager.LayoutParams.FLAG_FULLSCREEN;
        final int mask = full | WindowManager.LayoutParams.FLAG_FORCE_NOT_FULLSCREEN;
        if ((w.getAttributes().flags & mask) != full) w.setFlags(full, mask);
        if (Build.VERSION.SDK_INT >= 28) {
            WindowManager.LayoutParams lp = w.getAttributes();
            final int edges = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            if (lp.layoutInDisplayCutoutMode != edges) {
                lp.layoutInDisplayCutoutMode = edges;
                w.setAttributes(lp);
            }
        }
    }

    /**
     * Informa ao C as faixas cobertas pelo recorte da camera a cada novo layout da
     * janela (o recorte troca de lado ao girar 180 graus). Antes do Android 9 nao ha
     * recorte e o C fica com tudo 0.
     */
    private void watchCutout() {
        if (Build.VERSION.SDK_INT < 28) return;
        getWindow().getDecorView().setOnApplyWindowInsetsListener((v, insets) -> {
            reportCutout(insets);
            return v.onApplyWindowInsets(insets);
        });
    }

    @android.annotation.TargetApi(28)
    private void reportCutout(WindowInsets insets) {
        DisplayCutout c = insets.getDisplayCutout();
        int l = 0, t = 0, r = 0, b = 0;
        if (c != null) {
            l = c.getSafeInsetLeft();
            t = c.getSafeInsetTop();
            r = c.getSafeInsetRight();
            b = c.getSafeInsetBottom();
        }
        if (l == mInsets[0] && t == mInsets[1] && r == mInsets[2] && b == mInsets[3]) return;
        mInsets[0] = l;
        mInsets[1] = t;
        mInsets[2] = r;
        mInsets[3] = b;
        Log.v(TAG, "recorte da camera: esquerda " + l + ", alto " + t + ", direita " + r
                + ", baixo " + b);
        nativeSafeInsets(l, t, r, b);
    }

    /**
     * O HIDDeviceManager do SDL 2.32 registra um receptor para USB_PERMISSION (broadcast
     * que nao e do sistema) sem RECEIVER_EXPORTED/NOT_EXPORTED; com targetSdk 34+ o
     * Android 14 recusa com SecurityException e os controles USB via HIDAPI nao iniciam.
     * Aqui entra RECEIVER_NOT_EXPORTED, o que basta: o PendingIntent de USB_PERMISSION e
     * explicito (setPackage) e criado pelo proprio app, e USB conectado/desconectado e
     * Bluetooth ACL sao broadcasts protegidos do sistema, que chegam assim mesmo. Exportar
     * deixaria outro app forjar desconexoes e derrubar o controle.
     */
    @Override
    @SuppressLint("UnspecifiedRegisterReceiverFlag")   // antes da API 33 nao ha o sinalizador
    public Intent registerReceiver(BroadcastReceiver receiver, IntentFilter filter) {
        if (Build.VERSION.SDK_INT >= 33) {
            return super.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED);
        }
        return super.registerReceiver(receiver, filter);
    }

    /** Chamado pelo C (thread do SDL) via JNI: abre o seletor de documentos do sistema. */
    public void openFilePicker() {
        runOnUiThread(() -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("*/*");   // .tap/.tzx/.z80/.sna nao tem tipo MIME registrado
            try {
                startActivityForResult(i, REQ_OPEN);
            } catch (Exception e) {
                Log.e(TAG, "sem seletor de documentos", e);
                toast("Este aparelho não tem seletor de arquivos.");
                filePicked(null);
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQ_OPEN) return;
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            filePicked(null);
            return;
        }
        deliver(data.getData());
    }

    private void handleViewIntent(Intent intent) {
        if (!Intent.ACTION_VIEW.equals(intent.getAction())) return;
        Uri uri = intent.getData();
        if (uri != null) deliver(uri);
    }

    /** Copia o documento para o cache numa thread de trabalho e entrega o caminho ao C. */
    private void deliver(final Uri uri) {
        new Thread(() -> {
            synchronized (PICK_LOCK) {
                String name = displayName(uri);
                if (!supported(name)) {
                    toast("Formato não suportado: " + name
                            + "\nUse .tap .tzx .sna .z80 .scr .bin ou .cod");
                    filePicked(null);
                    return;
                }
                String path = copyToCache(uri, name);
                if (path == null) toast("Não foi possível ler " + name);
                filePicked(path);
            }
        }, "TK90X-picker").start();
    }

    /** Nome original do documento (sem separadores de diretorio). */
    private String displayName(Uri uri) {
        String name = null;
        if ("content".equals(uri.getScheme())) {
            try (Cursor c = getContentResolver().query(uri,
                    new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
                if (c != null && c.moveToFirst()) name = c.getString(0);
            } catch (Exception e) {
                Log.w(TAG, "sem DISPLAY_NAME: " + uri, e);
            }
        }
        if (name == null) name = uri.getLastPathSegment();
        if (name != null) {
            int slash = Math.max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
            if (slash >= 0) name = name.substring(slash + 1);
            name = name.replaceAll("[\\x00-\\x1f:*?\"<>|]", "_").trim();
            name = fitUtf8(name, MAX_NAME_BYTES - 1).trim();
            if (name.startsWith(".")) name = "_" + name;
        }
        return (name == null || name.isEmpty()) ? "arquivo" : name;
    }

    private static int utf8Len(String s) {
        return s.getBytes(StandardCharsets.UTF_8).length;
    }

    /**
     * Encurta o nome pelo comeco, mantendo a extensao, ate caber em maxBytes de UTF-8.
     * Conta bytes e nao caracteres (um ideograma ocupa 3 bytes) e anda por ponto de
     * codigo, para nunca partir um par substituto (emoji).
     */
    private static String fitUtf8(String name, int maxBytes) {
        if (utf8Len(name) <= maxBytes) return name;
        int dot = name.lastIndexOf('.');
        String ext = dot > 0 ? name.substring(dot) : "";
        if (utf8Len(ext) > 16) ext = "";           // nao e extensao: encurta o nome todo
        String base = ext.isEmpty() ? name : name.substring(0, dot);
        int extLen = utf8Len(ext);
        while (!base.isEmpty() && utf8Len(base) + extLen > maxBytes)
            base = base.substring(base.offsetByCodePoints(0, 1));
        return base + ext;
    }

    /** Extensao conhecida, ou nenhuma (o emulador reconhece pelo conteudo). */
    private static boolean supported(String name) {
        int dot = name.lastIndexOf('.');
        if (dot <= 0) return true;
        String ext = name.substring(dot + 1).toLowerCase(Locale.ROOT);
        for (String e : EXTENSIONS) if (e.equals(ext)) return true;
        return false;
    }

    private String copyToCache(Uri uri, String name) {
        File dir = new File(getCacheDir(), "picked");
        if (!dir.isDirectory() && !dir.mkdirs()) return null;
        // So guarda o ultimo arquivo aberto: o cache nao cresce sem limite.
        File[] old = dir.listFiles();
        if (old != null) for (File f : old) if (!f.getName().equals(name)) f.delete();
        File out = new File(dir, name);
        File tmp = new File(dir, "." + name + ".tmp");
        long total = 0;
        try (InputStream in = getContentResolver().openInputStream(uri);
             OutputStream os = new FileOutputStream(tmp)) {
            if (in == null) throw new java.io.IOException("openInputStream devolveu null");
            byte[] buf = new byte[65536];
            int n;
            while ((n = in.read(buf)) > 0) {
                total += n;
                if (total > MAX_FILE) throw new java.io.IOException("arquivo grande demais");
                os.write(buf, 0, n);
            }
        } catch (Exception e) {
            Log.e(TAG, "falha ao copiar " + uri, e);
            tmp.delete();
            return null;
        }
        if (!tmp.renameTo(out)) {
            tmp.delete();
            return null;
        }
        return out.getAbsolutePath();
    }

    private void toast(final String msg) {
        runOnUiThread(() -> Toast.makeText(this, msg, Toast.LENGTH_LONG).show());
    }
}
