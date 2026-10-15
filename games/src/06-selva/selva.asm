; =============================================================================
; selva.asm - "A Selva de Verdemor"
; Aventura de ação num labirinto de selva, vista de cima, para TK90X/TK95
; (ZX Spectrum 48K). Jogo original: mapa, desenhos, criaturas, textos e sons
; próprios.
;
; Licença MIT - (c) 2026 Projeto Emulador TK90X
;
; Montagem: ./build.sh  (gera gerado/selva.tap e copia para games/)
; Este arquivo contém só o que é deste jogo; o motor fica em motor/engine.asm.
; =============================================================================

        DEVICE ZXSPECTRUM48

; opções do motor LABIRINTO usadas por este jogo
        DEFINE HEDGE_EDGES
        DEFINE PROJ_DIRECTIONAL
        DEFINE BEH_STATIC
        DEFINE RANDOM_SPAWN
        DEFINE FLOWERS
        DEFINE SHADOW_ATTRS
        DEFINE GAME_TUNE
        DEFINE GAME_SHOOT_SND

ORIGEM  EQU 32768

; ---------------- carregador BASIC: 10 CLEAR 32767:LOAD ""CODE:RANDOMIZE USR 32768
        ORG 0xF000
basic:
        DB 0, 10
        DW linha_fim - linha_ini
linha_ini:
        DB 0xFD                         ; CLEAR
        DB "32767", 0x0E, 0, 0
        DW ORIGEM-1
        DB 0
        DB ':', 0xEF, '"', '"', 0xAF    ; :LOAD ""CODE
        DB ':', 0xF9, 0xC0              ; :RANDOMIZE USR
        DB "32768", 0x0E, 0, 0
        DW ORIGEM
        DB 0
        DB 0x0D
linha_fim:
basic_fim:

; ---------------- código ----------------
        ORG ORIGEM
inicio:
        include "motor/engine.asm"
        include "gerado/graficos.inc"
        include "gerado/mapa.inc"

; ---------------- cores ----------------
C_PRETO     EQU 0
C_AZUL      EQU 1
C_VERMELHO  EQU 2
C_MAGENTA   EQU 3
C_VERDE     EQU 4
C_CIANO     EQU 5
C_AMARELO   EQU 6
C_BRANCO    EQU 7
BRILHO      EQU 0x40
PISCA       EQU 0x80

; ---------------- regras ----------------
ENERGY_MAX      EQU 128
DRAIN_TICKS     EQU 50          ; 1 ponto de energia a cada 2 s (25 passos/s)
LIVES           EQU 3
RESPAWN_TICKS   EQU 75          ; 3 s (+ até 2,5 s) para a criatura voltar
PIECES_NEEDED   EQU 4
PROJ_RANGE      EQU 22          ; vida do dardo em passos: anda 21 x 4 = 84 pixels
WIN_BONUS       EQU 0x2000      ; pontos (BCD) por sair pelo portal

; ---------------- gráficos usados pelo motor ----------------
PLAYER_SPR      EQU SPR_NINA_CIMA0
PLAYER_INK      EQU BRILHO|C_BRANCO
PROJ_SPR8       EQU SPR8_DARDO_CIMA
PROJ_INK        EQU BRILHO|C_AMARELO
POOF_SPR        EQU SPR_PUF0
POOF_INK        EQU BRILHO|C_VERDE
GFX_WALLS       EQU TILE_sebe0
GFX_DOOR        EQU TILE_espinhos
GFX_MASTER      EQU TILE_portal
GFX_UP          EQU TILE_toca
GFX_DOWN        EQU TILE_toca
GFX_DECO1       EQU TILE_arvore
GFX_DECO2       EQU TILE_pedra
MASTER_ATTR     EQU PISCA|BRILHO|(C_VERMELHO<<3)|C_AMARELO
ICON_KEY        EQU CHR_ic_runa
ICON_PIECE      EQU CHR_ic_peca
ICON_EMPTY      EQU CHR_ic_vazio
ICON_FLOWER     EQU CHR_ic_flor
EFFECT_ROW      EQU 22
EFFECT_COL      EQU 31

; espinheiros trancados: vermelho, amarelo, branco (o 4º não é usado)
door_attr:
        DB BRILHO|C_VERMELHO, BRILHO|C_AMARELO, BRILHO|C_BRANCO, BRILHO|C_CIANO
key_icon_attr:
        DB BRILHO|C_VERMELHO, BRILHO|C_AMARELO, BRILHO|C_BRANCO, BRILHO|C_CIANO

; ---------------- flores ----------------
; índice = efeito: 1 velocidade (amarela), 2 invulnerável (branca, cintila
; com ciano), 3 confusão (magenta)
effect_ink:
        DB 0, BRILHO|C_AMARELO, BRILHO|C_BRANCO, BRILHO|C_MAGENTA
effect_ticks:
        DB 0, 250, 200, 150     ; 10 s, 8 s, 6 s

; ---------------- criaturas ----------------
; sprite, tinta, comportamento (0 atravessa as sebes, 1 voo quicando,
; 2 persegue contornando, 3 parada), máscara de velocidade (anda quando
; passo AND máscara = 0), vida, dano, pontos (BCD)
NUM_ETYPES      EQU 4
enemy_types:
        DB SPR_NEVOA0,  BRILHO|C_CIANO,    0, 1, 1, 12
        DW 0x0050
        DB SPR_ZUNZUM0, BRILHO|C_MAGENTA,  1, 0, 1, 8
        DW 0x0030
        DB SPR_CASCO0,  BRILHO|C_AMARELO,  2, 1, 2, 16
        DW 0x0100
        DB SPR_BOCAO0,  BRILHO|C_VERMELHO, 3, 0, 3, 20
        DW 0x0150

; ---------------- objetos ----------------
; efeito (0 runa, 1 pedaço do disco, 2 comida, 3 flor), valor (runa / energia /
; efeito), tinta, 0, desenho, pontos (BCD)
item_kinds:
        DB 0, 0, BRILHO|C_VERMELHO, 0
        DW ITEM_runa, 0x0100
        DB 0, 1, BRILHO|C_AMARELO, 0
        DW ITEM_runa, 0x0100
        DB 0, 2, BRILHO|C_BRANCO, 0
        DW ITEM_runa, 0x0100
        DB 1, 0, PISCA|BRILHO|C_AMARELO, 0
        DW ITEM_peca, 0x0500
        DB 2, 40, BRILHO|C_AMARELO, 0
        DW ITEM_manga, 0x0050
        DB 2, 24, BRILHO|C_AMARELO, 0
        DW ITEM_banana, 0x0050
        DB 2, 32, C_AMARELO, 0
        DW ITEM_coco, 0x0050
        DB 3, 1, BRILHO|C_AMARELO, 0
        DW ITEM_flor_vento, 0x0010
        DB 3, 2, BRILHO|C_BRANCO, 0
        DW ITEM_flor_cristal, 0x0010
        DB 3, 3, BRILHO|C_MAGENTA, 0
        DW ITEM_flor_tonta, 0x0010

; ---------------- painel ----------------
HUD_ATTR        EQU BRILHO|C_BRANCO
HUD_NAME_ATTR   EQU BRILHO|(C_VERDE<<3)|C_PRETO
HUD_FLOOR_ATTR  EQU BRILHO|C_VERDE
BAR_ATTR_OK     EQU BRILHO|C_AMARELO
BAR_ATTR_LOW    EQU PISCA|BRILHO|C_VERMELHO
PIECE_ATTR      EQU BRILHO|C_AMARELO
EMPTY_ATTR      EQU C_BRANCO

hud_labels:
        DB 2,HUD_ATTR
        DB 1,21,0,"ENERGIA", 1,21,25,"VIDAS"
        DB 1,22,0,"PONTOS",  1,22,16,"RUNAS"
        DB 1,23,0,"DISCO"
        DB 0

; ---------------- som ----------------
snd_shoot:                      ; sopro da zarabatana: chiado curto
        ld hl,0x0800
        ld bc,90
.l:     ld a,(hl)
        and 0x10
        out (0xFE),a
        inc hl
        ld e,2
.d:     dec e
        jr nz,.d
        dec bc
        ld a,b
        or c
        jr nz,.l
        ret

victory_tune:                   ; melodia original curta (meio-período, meios-ciclos)
        DB 100,70, 80,88, 67,104, 80,88, 67,104, 50,140, 60,116, 50,240, 0,0

; ---------------- telas ----------------
TITLE_ATTR          EQU C_BRANCO
TITLE_PAGE_ROW      EQU 5
TITLE_PAGE_ROWS     EQU 13
TITLE_PAGE_FRAMES   EQU 450
END_SCORE_ROW       EQU 16
END_SCORE_COL       EQU 17

game_title:
        DB 2,BRILHO|C_AMARELO, 1,0,11, "A SELVA DE"
        DB 3, 2,BRILHO|C_VERDE, 1,1,12, "VERDEMOR", 4
        DB 2,C_AMARELO, 1,3,8, "e o Disco de Âmbar"
        DB 2,C_VERDE, 1,4,0, "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
        DB 2,PISCA|BRILHO|C_BRANCO, 1,20,1, "ESPAÇO, 0 ou ENTER para jogar"
        DB 2,BRILHO|C_VERDE, 1,22,5, "Projeto Emulador TK90X"
        DB 2,C_VERDE, 1,23,2, "Licença MIT - jogo original"
        DB 0

game_page1:
        DB 2,BRILHO|C_BRANCO, 1,5,0
        DB "Nina Ventura, cartógrafa, caiu",13
        DB "com seu balão na Selva de",13
        DB "Verdemor: um labirinto vivo de",13
        DB "sebes que se fecha atrás de quem",13
        DB "entra.",13
        DB 2,BRILHO|C_AMARELO
        DB "A única saída é o portal do",13
        DB "Templo das Mil Raízes, que só",13
        DB "abre com o Disco de Âmbar. O",13
        DB "Guardião da selva partiu o disco",13
        DB "em 4 pedaços e os escondeu.",13
        DB 2,BRILHO|C_VERDE
        DB "Sopre dardos com a zarabatana,",13
        DB "coma frutas e cheire as flores!",0

game_page2:
        DB 2,BRILHO|C_AMARELO, 1,5,11, "CONTROLES"
        DB 2,BRILHO|C_BRANCO, 1,6,0
        DB "Q cima  A baixo  O esq.  P dir.",13
        DB "ESPAÇO ou 0: sopra um dardo",13
        DB "Joystick Sinclair: 6 7 8 9 e 0",13
        DB "ou Cursor: 5 6 7 8 e 0",13
        DB 2,C_BRANCO, 1,12,1, "Nina", 1,12,6, "Névoa", 1,12,12, "Zunzum", 1,12,19, "Casco", 1,12,25, "Bocão"
        DB 2,BRILHO|C_AMARELO, 1,14,0, "Flor amarela: corre mais rápido"
        DB 2,BRILHO|C_BRANCO, 1,15,0, "Flor branca: nada a machuca"
        DB 2,BRILHO|C_MAGENTA, 1,16,0, "Flor roxa: confunde as direções"
        DB 2,BRILHO|C_VERDE, 1,17,0, "Runas desfazem espinheiros."
        DB 0

; desenha os personagens na página 2 (linhas 10-11)
game_page2_hook:
        ld hl,vitrine
.l:     ld a,(hl)
        cp 0xFF
        ret z
        push hl
        call spr16_ptr
        ex de,hl
        pop hl
        inc hl
        ld c,(hl)
        inc hl
        ld a,(hl)
        inc hl
        push hl
        push af
        ex de,hl
        ld b,80
        xor a
        push bc
        call draw_spr
        pop bc
        pop af                      ; A = tinta
        ld e,a
        ld a,c                      ; coluna = x/8
        rrca
        rrca
        rrca
        and 31
        ld l,a
        ld h,0
        ld bc,ATTRS+10*32
        add hl,bc
        ld (hl),e
        inc hl
        ld (hl),e
        ld bc,31
        add hl,bc
        ld (hl),e
        inc hl
        ld (hl),e
        pop hl
        jr .l
vitrine:
        DB SPR_NINA_BAIXO0, 16, PLAYER_INK
        DB SPR_NEVOA0, 64, BRILHO|C_CIANO
        DB SPR_ZUNZUM0, 112, BRILHO|C_MAGENTA
        DB SPR_CASCO0, 160, BRILHO|C_AMARELO
        DB SPR_BOCAO0, 208, BRILHO|C_VERMELHO
        DB 0xFF

txt_mode0:
        DB 2,BRILHO|C_CIANO, 1,19,1, "Teclas 1/2 joystick: SINCLAIR",0
txt_mode1:
        DB 2,BRILHO|C_CIANO, 1,19,1, "Teclas 1/2 joystick: CURSOR  ",0

txt_victory:
        DB 3, 2,BRILHO|C_AMARELO, 1,2,8, "O PORTAL ABRIU!", 4
        DB 2,BRILHO|C_BRANCO, 1,6,1
        DB "Nina encaixa os 4 pedaços do",13
        DB "Disco de Âmbar. As raízes do",13
        DB "templo se afastam e uma luz",13
        DB "dourada mostra o caminho de",13
        DB "volta para casa. O mapa da",13
        DB "Selva de Verdemor está pronto!",13
        DB 2,BRILHO|C_VERDE, 1,16,9, "PONTOS:"
        DB 2,BRILHO|C_CIANO, 1,21,4, "Tecle algo para continuar"
        DB 2,BRILHO|C_AMARELO
        DB 0

txt_gameover:
        DB 3, 2,BRILHO|C_VERMELHO, 1,2,10, "FIM DE JOGO", 4
        DB 2,BRILHO|C_BRANCO, 1,6,1
        DB "As forças de Nina acabaram e",13
        DB "as sebes de Verdemor se",13
        DB "fecharam em volta dela.",13
        DB 13
        DB "Tente outra vez: o Disco de",13
        DB "Âmbar continua à espera!",13
        DB 2,BRILHO|C_VERDE, 1,16,9, "PONTOS:"
        DB 2,BRILHO|C_CIANO, 1,21,4, "Tecle algo para continuar"
        DB 2,BRILHO|C_AMARELO
        DB 0

fim:
        ASSERT fim <= SHBUF16

        EMPTYTAP "gerado/selva.tap"
        SAVETAP  "gerado/selva.tap", BASIC, "Verdemor", basic, basic_fim - basic, 10
        SAVETAP  "gerado/selva.tap", CODE,  "verdemor", inicio, fim - inicio
