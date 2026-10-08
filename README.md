# MasterAgent

Plugin VST3 di **sola analisi** per mix e mastering. Si inserisce come ultimo plugin sul mix bus (mentre mixi) o sul master bus, misura tutti i parametri e li confronta con uno standard di mercato o con uno o più brani di riferimento, con consigli di correzione concreti.
L'audio passa **inalterato** (bit-transparent, latenza 0). Le correzioni le fai tu con i tuoi plugin.

## Installazione
1. Compila (vedi sotto) oppure prendi `build/MasterAgent_artefacts/Release/VST3/MasterAgent.vst3`.
2. Copia la cartella `MasterAgent.vst3` in `C:\Program Files\Common Files\VST3\`.
3. Fai un rescan dei plugin nella DAW e inserisci **MasterAgent** come **ultimo slot** del master, dopo il limiter.

## Flusso di lavoro consigliato
1. Scegli il **Target** in alto: un profilo di mercato (Streaming, Spotify, Apple Music, YouTube, Pop, Rock, Hip-hop/Trap, EDM/Club, Techno/House), un profilo creato da brani di riferimento oppure "Brano di riferimento". Scegli anche la **fase**: **Mix** se il plugin è sul mix bus mentre mixi, **Master** se è sul master.
2. Con *Auto-reset al play* attivo, avvia la riproduzione **dall'inizio del brano** e lasciala andare **fino alla fine**. Integrated, LRA, DR e PSR minimo sono affidabili solo sul brano intero.
3. Leggi il pannello **Report**. Le diagnosi sono ordinate per gravità (rosso = critico, giallo = da verificare) e ognuna ha un suggerimento su cosa intervenire.
4. Correggi con i tuoi plugin, premi **Reset** e riproduci di nuovo.
5. Con **Esporta report** salvi il report in TXT o JSON, oppure lo screenshot della dashboard in PNG.

### Fase di lavoro: Mix o Master
- **Master**: confronto completo con il target (loudness, true peak, PLR/PSR/DR, tonale, stereo). Le correzioni sono pensate per il bus stereo e restano piccole: in mastering si corregge raramente più di 1-2 dB (S. Savage, *Mixing and Mastering in the Box*, 11.2), il resto viene indicato come "da risolvere nel mix".
- **Mix**: loudness, true peak e densità del master finito non vengono confrontati con il target, perché li deciderà il mastering. Al loro posto ci sono due controlli di consegna:
  - **margine per il mastering**: picchi tra -6 e -3 dBTP, con quanto abbassare il master fader;
  - **limiting sul mix bus**: con PLR sotto 9 dB il mix risulta già limitato. Puoi ascoltare con il limiter, ma consegna senza (Savage, 8.1).

  I consigli tonali indicano le **tracce** che di solito causano il problema in quella banda e cosa fare su di esse; la mossa EQ equivalente sul bus resta come riferimento. Esempio per il Low-mid: chitarre ritmiche, piano, pad, effetto prossimità delle voci e code dei riverberi, da trattare con un taglio a 200-400 Hz e un high-pass sui ritorni FX.

### Consigli di correzione
- **Mosse EQ concrete**: lo scostamento tonale viene approssimato con al massimo 3 (master) o 4 (mix) mosse, ognuna con tipo, frequenza, guadagno e Q (es. `Bell 315 Hz -1.5 dB, Q 1.0` o `High shelf 8 kHz -1.0 dB`).
  - Più bande fuori target con la stessa causa diventano una sola mossa: per esempio Brilliance + Air + tilt troppo brillanti si risolvono con un high shelf.
  - La curva suggerita è disegnata sullo spettro (tratteggio blu, scala ±6 dB, nodi numerati).
  - Con le curve generiche dei profili di mercato il guadagno consigliato è dimezzato.
- **Costante, a raffiche o in una sezione**: per ogni banda in eccesso il plugin misura com'è distribuita l'energia nel tempo.
  - **Costante** → EQ statico.
  - **A raffiche** (sibilanti, singole note che rimbombano, frasi aspre) → de-esser, EQ dinamico o multibanda (Savage, 4.4).
  - **Concentrata in una sezione** → automazione o EQ solo in quel punto.
- **Dove**: quando l'eccesso è localizzato, la diagnosi indica il tratto del brano (es. "concentrato tra 1:32 e 1:48"). Con l'hover sulla diagnosi il tratto si illumina sulla cronologia della loudness. Il tempo è quello del transport della DAW, quindi è corretto anche se non parti dall'inizio.
- **Note o risonanze**: i picchi stretti e persistenti vengono confrontati con la griglia delle note, dopo aver stimato l'intonazione.
  - Se coincidono con una nota portante del brano (tonica, basso, accordo principale) sono solo un'informazione, con il nome della nota (es. "La (A2)"): non vanno corretti con un EQ statico.
  - Gli altri sono **risonanze** da attenuare con un EQ stretto.
  - Un picco presente anche nel reference caricato non viene segnalato come difetto.
- **Da fare prima, in ordine**: le tre priorità sono i problemi più gravi, mostrati nell'ordine in cui conviene affrontarli: fase e tecnici, poi low-end, tonale, dinamica e per ultimi loudness e ceiling.
  - Diagnosi diverse con la stessa soluzione occupano una sola priorità. Per esempio PLR, PSR e integrated troppo alto si risolvono tutti con meno limiting.

### Modalità di analisi
- **Brano intero**: le misure si accumulano dall'ultimo reset. Serve per il verdetto finale: riproduci il brano dall'inizio alla fine.
- **Live**: finestra mobile sugli ultimi secondi (10/20/30/60 s, da *Opzioni*). Mentre regoli limiter, EQ o imager vedi subito l'effetto, senza premere Reset. In Live LRA e DR sono esclusi, perché descrivono il brano intero. Il bilanciamento tonale della finestra (spesso un ritornello in loop) viene confrontato con la **sezione più forte** del reference, non con il brano intero.
- **Confronto tonale** (da *Opzioni*): sul brano intero oppure solo sulle sezioni più forti (ritornello/drop) di master e target. Il secondo è più affidabile quando intro, strofe e breakdown dei due brani sono arrangiati in modo diverso.

### Analizzare un file come master
Invece di riprodurre il brano nella DAW, puoi far analizzare direttamente il bounce: *Opzioni → Analizza un file come master…*, oppure trascina il file sulla finestra e scegli *Analizza come master*.
- L'analisi del brano intero richiede pochi secondi (stesso motore, letto a blocchi senza caricare il file in memoria).
- Un banner mostra l'avanzamento; finita l'analisi la dashboard mostra il file (badge **FILE**) al posto dell'ingresso live, che intanto continua a essere analizzato. Clic sul banner per tornare al live.
- Il percorso del file è salvato nel progetto: alla riapertura viene rianalizzato.

### Versioni del master
Per sapere se una correzione ha davvero migliorato il master:
1. Premi **Salva versione** nel Report: l'analisi attuale diventa una versione (es. *v1 14:32*, con il nome del file se analizzi un file).
2. Fai le correzioni e rianalizza (riproduci di nuovo il brano o analizza il nuovo bounce).
3. In cima al Report compare **Rispetto a v1**: quante diagnosi sono migliorate, peggiorate o invariate, il punteggio prima e dopo e le singole differenze (prima i peggioramenti).

Una diagnosi migliora se scende di gravità o, a parità di gravità, si avvicina al range in modo apprezzabile. La versione confrontata compare anche sullo spettro (puntinato grigio) e sulla cronologia della loudness (linea grigia tenue). Con il selettore *Confronta con* scegli la versione; da *Opzioni → Versioni* puoi rinominarla o eliminarla. Si tengono le ultime 8 versioni, salvate nel progetto (pochi KB l'una).

### Coerenza album
Per un album, un EP o una serie di singoli: *Opzioni → Controlla coerenza album…*, oppure trascina 2-30 file sulla finestra e scegli *Controlla la coerenza dell'album*. I brani vengono analizzati in background, uno alla volta; poi un pannello sopra la dashboard confronta ogni brano con la **mediana dell'album**. La mediana, al contrario della media, non si sposta per un solo brano fuori posto, che così risulta segnalato da solo.
- **Sezione più forte** (max short-term, cioè ritornello o drop): oltre ±1,5 LU dalla mediana è da verificare, oltre ±3 LU è critico. La colonna *Gain* indica di quanto alzare o abbassare il brano; se il true peak non lascia spazio, il consiglio è spingere il limiter. L'integrated è solo informativa: le ballate possono essere volutamente più basse.
- **Bilanciamento tonale** per banda rispetto alla curva mediana dell'album, confrontando i ritornelli quando tutti i brani ne hanno uno. La tolleranza è di almeno 1,5 dB, più larga su Sub e Air. Le bande vicine spostate nella stessa direzione diventano una sola diagnosi con la correzione (shelf o campana larga, al massimo ±4 dB: oltre conviene intervenire nel mix).
- **Ceiling true peak** diverso di oltre 1 dB e **larghezza stereo** diversa di oltre 8 punti: note informative (in azzurro).

Clic su una riga per vedere a destra le diagnosi del brano e il suo tonale rispetto all'album; doppio clic (o *Mostra nella dashboard*) per aprirlo nella dashboard come file master, con l'analisi completa sul target attivo. *Esporta* salva la tabella come testo, CSV o immagine. I risultati restano nel plugin finché non premi *Svuota* (non sono salvati nel progetto); *Chiudi* torna alla dashboard e il pannello si riapre da *Opzioni*. Dalla riga di comando: `MasterAgentAnalyze --album brano1.wav brano2.wav ...`.

### Spettro Mid/Side
Il selettore **L+R | M/S** in alto a sinistra sullo spettro passa alla vista Mid/Side: Mid (ciò che sta al centro) e Side (la differenza tra i canali).
- Più il Side si avvicina al Mid, più quella zona è larga.
- Sotto 120 Hz le colonne arancio segnalano bassi troppo larghi (Side a meno di 6 dB dal Mid).
- Il Side del reference (tratteggio viola) è portato al livello del master per confrontare la larghezza, non il volume.

### Interazione
- **Hover su una diagnosi** del report: l'elemento collegato si illumina sulla dashboard (valore, banda, riga o meter).
- **Hover su una banda** (Sub, Bass, ...) o sullo spettro: la zona di frequenze si evidenzia, e un cursore mostra frequenza, livello e scostamento dal target.
- **Da fare prima**: in cima al report ci sono al massimo 3 interventi prioritari.
- **Pagella**: accanto al punteggio c'è un voto per area (Loudness e picchi, Dinamica, Tonale, Stereo, Tecnici).
  - Ogni misura valutata pesa allo stesso modo nella sua area: una critica pesa 2,5 volte una da verificare.
  - Il punteggio è la media pesata dei voti (il tonale pesa un po' di più, lo stereo un po' meno). Un problema tecnico critico (clipping, fase invertita, canale muto) lo limita a 60.
  - Clic su un'area per vedere solo le sue diagnosi; di nuovo per vederle tutte.
- **Ignora diagnosi**: clic destro su una diagnosi → *Ignora: è una scelta voluta* (per esempio uno stereo molto largo voluto). Esce da voti, priorità e contatori, non colora più la dashboard e finisce in fondo tra le *Ignorate*. La scelta resta salvata nel progetto; si annulla con il clic destro o da *Opzioni*.
- **Guida "?"** (in alto a destra): attivala e lascia il mouse fermo per un secondo su un valore, un grafico o una colonna. Compare cosa misura, come leggerlo e quali valori aspettarsi. Fuori dai valori, la guida spiega il pannello intero. Si attiva anche da *Opzioni*.
- **Schermo intero** (icona accanto a "?"): la dashboard occupa tutto il monitor su cui si trova la finestra del plugin. Per tornare premi **Esc** (o F11), di nuovo l'icona oppure clicca nella finestra del plugin nella DAW.

### Interfaccia (Opzioni > Interfaccia)
- **Tema**: Scuro (default), Chiaro, Grafite (grigi neutri) e Alto contrasto (nero pieno, testi e stati più saturi).
- **Colore accento**: Azzurro, Turchese, Indaco o Argento. Nel tema chiaro si usano tinte più scure, così restano leggibili.
- **Ombre e sfumature**: disattivandole si passa a uno stile piatto.
- **Dimensione interfaccia**: dal 75 al 150%. Sotto il 100% la finestra minima si riduce (a 75%: 1050×615), utile sui portatili; sopra il 100% testi e grafici diventano più grandi, utile a schermo intero su monitor grandi.
- Avvisi e finestre di dialogo seguono il tema scelto.
- Le preferenze valgono per tutte le istanze del plugin e vengono salvate in `%APPDATA%\MasterAgent\ui.settings`.

### Export offline (Export Mixdown)
Durante un render offline il plugin attende l'analisi invece di perdere audio. Un export analizzato è quindi completo, al prezzo di qualche secondo in più (circa 28× il tempo reale).

### Brano di riferimento
- **Caricamento:** usa "Carica reference..." oppure trascina un file sulla finestra e scegli *Usa come brano di riferimento*. Formati: WAV, AIFF, FLAC, MP3, OGG, fino a 20 minuti. Trascinando più file insieme puoi crearne subito un profilo.
- **Confronto:** il reference viene analizzato in background con lo stesso motore del master. Il confronto mostra la differenza per ogni metrica, la curva spettrale sovrapposta e la larghezza stereo per banda.
- **Ascolta ref (A/B):** l'uscita del plugin diventa il reference, già allineato in loudness al tuo master, così il confronto a orecchio è onesto. Un banner viola resta visibile finché è attivo. **Disattivalo prima dell'export!**
- **Salva profilo:** trasforma l'analisi del reference in un profilo target riutilizzabile, salvato in `%APPDATA%\MasterAgent\profiles`.

### Profilo da più brani di riferimento
*Opzioni → Crea profilo da più brani di riferimento…*: scegli 3-5 release con l'estetica che cerchi. Vengono analizzate in background, una alla volta, con lo stesso motore.
- Il **target** è la loro media.
- Le **tolleranze di ogni banda** derivano dalla dispersione reale tra i brani (deviazione standard), non da valori fissi. Un genere con bassi molto variabili tollera di più sui bassi, uno coerente è più severo.
- Il profilo memorizza anche la curva della sezione più forte e il comportamento "a raffiche" tipico di ogni banda.

È il modo più preciso per avere un target di genere. Dalla riga di comando lo stesso profilo si crea con `MasterAgentAnalyze --make-profile "Nome" out.json brano1.wav brano2.wav ...`.

## Cosa misura
| Area | Metriche |
|---|---|
| Loudness | Integrated, Short-term, Momentary, max M/ST, LRA (ITU-R BS.1770-4, EBU R128, Tech 3341/3342), cronologia dello short-term |
| Peak | True Peak per canale (oversampling 4× a 44.1/48 kHz, filtro Kaiser da 192 tap), sample peak, eventi oltre -1 e 0 dBTP |
| Dinamica | PLR, PSR attuale e minimo, DR (algoritmo TT/Pleasurize), crest factor full-band e per banda (<200 Hz, medi, >4 kHz), istogramma della loudness |
| Tonale | Spettro medio e istantaneo, terzi d'ottava rispetto alla curva target (brano intero e sezione più forte), scostamento su 8 bande, mosse EQ suggerite, tilt (dB/oct), centroide, energia sotto i 30 Hz, picchi stretti classificati come note (con intonazione e tonica stimate) o risonanze |
| Nel tempo | Per ogni banda: quanto l'energia arriva a raffiche e la zona del brano in cui è più in eccesso |
| Stereo | Correlazione istantanea, media e per banda; larghezza S/(M+S) globale e per banda; bilanciamento L/R; perdita in mono; goniometro |
| Tecnici | Clipping (plateau di campioni), DC offset, noise floor, canale muto, fase invertita |
| Streaming | Guadagno che applicheranno Spotify, Apple Music, YouTube, Tidal, Amazon e Deezer, con la loudness e il true peak risultanti |

### Note sulla precisione
- Loudness e LRA sono verificati sui casi di test EBU Tech 3341/3342: scarto ≤ 0.1 LU per la loudness e ≤ 1 LU per l'LRA, a 44.1, 48 e 96 kHz.
- Il true peak è verificato su sinusoidi a fs/4 con fase a 45°, il caso peggiore per gli inter-sample peak. Scarto ≤ 0.15 dB.
- **Le curve tonali dei profili di mercato sono modelli generici** (pendenza, enfasi sui bassi, roll-off). Per un riferimento tonale affidabile usa un reference o un profilo creato da uno o più reference.
- Il confronto tonale allinea master e target sulla **mediana** degli scostamenti: un eccesso forte in una zona (es. sibilanti) non fa sembrare carenti tutte le altre bande. Gli intervalli di loudness e dinamica riflettono la prassi attuale del mercato e si possono modificare nei file JSON.
- *Analizza solo in play* (attivo di default, in *Opzioni*) ignora l'audio quando il transport è fermo.
- La trasparenza è verificata bit per bit sul VST3 reale: 44.1, 48 e 96 kHz; mono e stereo; blocchi da 1 a 4096 campioni e variabili. Il costo sull'audio thread è circa 0.05% del budget di un blocco da 256 campioni.

## Compilazione
Requisiti: Visual Studio 2022 (o Build Tools), CMake ≥ 3.22. JUCE 8.0.15 e Catch2 si trovano in `external/`.
```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build\MasterAgentTests_artefacts\Release\MasterAgentTests.exe      # test unitari
```
Target disponibili:
- `MasterAgent_VST3`
- `MasterAgent_Standalone`
- `MasterAgentTests` (test di conformità e di coerenza delle diagnosi)
- `MasterAgentPreview` (render della dashboard in PNG con segnali sintetici)
- `MasterAgentAnalyze` (analizza file audio con lo stesso motore e stampa mosse EQ e diagnosi; variabili `MA_PROFILE`, `MA_PROFILE_FILE`, `MA_PHASE=mix|master`; `--make-profile` per i profili da più brani; `--album` per la coerenza album, esce con codice 2 se c'è un brano critico)
- `MasterAgentHostTest` (carica il VST3 reale e verifica bit-transparency, CPU, render offline e stato):
  `build\MasterAgentHostTest_artefacts\Release\MasterAgentHostTest.exe build\MasterAgent_artefacts\Release\VST3\MasterAgent.vst3`

I profili di mercato si rigenerano con `python tools/generate_profiles.py`.

## Struttura
```
Source/Analysis/   motori di analisi (loudness, true peak, dinamica, spettro/stereo, tecnici, streaming, reference)
Source/Compare/    profili target, confronto e diagnosi, mosse EQ suggerite, export del report
Source/UI/         pannelli della dashboard
Resources/profiles profili di mercato (JSON)
Tests/             test Catch2 con segnali sintetici
```
