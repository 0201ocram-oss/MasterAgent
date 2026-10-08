#include "HelpTexts.h"
#include "../Analysis/AnalysisSnapshot.h"
#include "../Common/Text.h"
#include "../Compare/Comparator.h"

#include <map>

namespace ma::ui
{

namespace
{
    const std::map<juce::String, const char*>& texts()
    {
        static const std::map<juce::String, const char*> map {
            // --- pannelli (testo generico quando il mouse non è su un valore) ---------------------------
            { "panel:loudness",  "Loudness (EBU R128 / BS.1770-4)\nIl volume percepito del brano: integrated, short-term, momentary, "
                                 "LRA e la cronologia nel tempo. Passa il mouse su un valore per sapere cosa misura." },
            { "panel:peak",      "Peak e controlli tecnici\nPicchi reali (true peak) e digitali, eventi oltre la soglia, clipping, "
                                 "DC offset e rumore di fondo: tutto ciò che può distorcere o sprecare headroom." },
            { "panel:dynamics",  "Dinamica\nQuanto il brano è compresso o limitato: PLR, PSR, DR, crest factor per banda e la "
                                 "distribuzione della loudness nel tempo." },
            { "panel:spectrum",  "Spettro e bilanciamento tonale\nCome si distribuisce l'energia tra le frequenze, confrontata con il "
                                 "target o il reference, con le mosse EQ suggerite e lo scostamento per banda." },
            { "panel:stereo",    "Stereo e fase\nImmagine stereo, correlazione di fase, larghezza per banda e compatibilità mono." },
            { "panel:streaming", "Anteprima normalizzazione streaming\nCome ogni piattaforma riprodurrà il brano dopo aver uniformato "
                                 "il volume: quanto lo abbassa o lo alza e che true peak ne risulta." },
            { "panel:report",    "Report e diagnosi\nIl verdetto: punteggio, problemi trovati ordinati per gravità e cosa fare. "
                                 "Passa il mouse su una diagnosi per evidenziare l'elemento collegato nella dashboard." },

            // --- loudness -----------------------------------------------------------------------------
            { "integrated",   "Integrated (LUFS)\nVolume percepito medio di tutto il brano dall'ultimo reset, con il gating di "
                              "BS.1770-4: i passaggi quasi silenziosi non contano. È il valore che le piattaforme usano per la "
                              "normalizzazione (Spotify e YouTube -14, Apple Music -16). In Live è la media della sola finestra." },
            { "shortTerm",    "Short-term (LUFS)\nLoudness su una finestra mobile di 3 secondi. Segue le sezioni del brano: "
                              "utile per confrontare strofa, ritornello e drop." },
            { "momentary",    "Momentary (LUFS)\nLoudness su una finestra di 400 ms: reagisce a singoli colpi e frasi." },
            { "lra",          "LRA - Loudness Range (LU)\nQuanto varia la loudness tra le parti piane e quelle forti del brano "
                              "(dal 10° al 95° percentile della short-term, EBU Tech 3342). Valori bassi = brano compatto e "
                              "costante; alti = dinamica ampia tra le sezioni. Descrive il brano intero: in Live non è disponibile." },
            { "maxShortTerm", "Max short-term (LUFS)\nIl valore più alto della short-term: la loudness della sezione più forte "
                              "(ritornello o drop). Utile per confrontare brani con dinamiche diverse." },
            { "maxMomentary", "Max momentary (LUFS)\nIl valore più alto della momentary dall'ultimo reset." },
            { "time",         "Tempo analizzato\nQuanto audio è stato misurato dall'ultimo reset. Integrated, LRA e DR sono "
                              "affidabili solo dopo aver riprodotto il brano intero. In Live mostra la durata della finestra." },
            { "history",      "Cronologia della loudness\nShort-term nel tempo, un punto ogni 100 ms. Fascia gialla = range "
                              "integrated del target, tratteggio = integrated attuale, linea viola = integrated del reference. "
                              "Le diagnosi legate a un tratto del brano lo evidenziano qui. La linea grigia tenue è la versione salvata "
                              "con cui stai confrontando il master." },

            // --- peak e tecnici --------------------------------------------------------------------------
            { "truePeak",     "True peak max (dBTP)\nIl picco più alto del segnale ricostruito (sovracampionato, BS.1770-4), "
                              "cioè il livello che raggiungerà dopo la conversione D/A o la codifica MP3/AAC. Per lo streaming "
                              "si consiglia di restare sotto -1 dBTP." },
            { "tpMeters",     "Meter true peak L / R\nLivello attuale del picco ricostruito per ciascun canale. La scala diventa "
                              "rossa vicino a 0 dBTP." },
            { "samplePeak",   "Sample peak L / R (dBFS)\nPicco più alto dei campioni digitali per canale. Può essere più basso del "
                              "true peak perché non vede i picchi tra un campione e l'altro (inter-sample)." },
            { "recentTp",     "TP recente (3 s)\nTrue peak massimo degli ultimi 3 secondi: mostra subito l'effetto di una "
                              "modifica al limiter." },
            { "overs1",       "Eventi > -1 dBTP\nQuante volte il true peak ha superato -1 dBTP, la zona in cui la codifica "
                              "lossy delle piattaforme può introdurre distorsione." },
            { "overs",        "Eventi > 0 dBTP\nQuante volte il true peak ha superato 0 dBTP: picchi inter-sample che "
                              "distorceranno nella conversione. Dovrebbero essere zero: abbassa il ceiling del limiter o attiva "
                              "la sua modalità true peak." },
            { "clip",         "Clipping\nTratti con 3 o più campioni consecutivi a fondo scala: il segnale è già stato tagliato "
                              "(clipping digitale) prima di arrivare al plugin." },
            { "dc",           "DC offset max (dBFS)\nComponente continua (0 Hz) del segnale. Sopra -60 dBFS riduce l'headroom e "
                              "può creare click nei tagli: si elimina con un high-pass a 5-20 Hz." },
            { "noiseFloor",   "Noise floor (dBFS)\nLivello dei passaggi più silenziosi: rivela fruscio, ronzii o code che non "
                              "si spengono nelle pause." },
            { "sampleRate",   "Sample rate\nFrequenza di campionamento della sessione nella DAW." },
            { "refTp",        "Reference TP max\nTrue peak massimo del brano di riferimento caricato." },

            // --- dinamica -----------------------------------------------------------------------------------
            { "plr",          "PLR - Peak to Loudness Ratio (dB)\nTrue peak max meno integrated: quanto il brano è compresso e "
                              "limitato nel complesso. Più è basso, più il master è schiacciato; valori alti indicano un brano "
                              "dinamico. Il range adatto dipende dal genere (vedi il target)." },
            { "psr",          "PSR minimo (dB)\nPeak to Short-term Ratio più basso registrato: true peak recente meno short-term "
                              "nella sezione più compressa del brano. Valori molto bassi indicano un punto schiacciato che può "
                              "risultare affaticante." },
            { "dr",           "DR - Dynamic Range\nIndice di dinamica dell'algoritmo TT/Pleasurize (lo stesso del DR Database): "
                              "differenza tra picchi e RMS delle parti più forti del brano. Più alto = più dinamico. Descrive il "
                              "brano intero: in Live non è disponibile." },
            { "psrNow",       "PSR attuale (dB)\nTrue peak degli ultimi 3 secondi meno short-term: quanto respira la sezione "
                              "che stai ascoltando adesso." },
            { "crestFactor",  "Crest factor (dB)\nSample peak meno RMS: quanto i transienti emergono sopra il livello medio." },
            { "rms",          "RMS (dBFS)\nLivello medio del segnale senza pesatura, media dei due canali." },
            { "stHistogram",  "Distribuzione della loudness\nQuanto tempo il brano passa a ogni livello di short-term. Una campana "
                              "stretta = loudness costante; larga = parti piane e parti forti. Le tacche viola sono il reference." },

            // --- spettro ----------------------------------------------------------------------------------
            { "spectrum:view", "Vista dello spettro\nL+R: spettro del master con target, reference ed EQ suggerito. "
                               "M/S: spettro del Mid (ciò che è al centro) e del Side (la differenza tra i canali), per vedere "
                               "dove si allarga l'immagine stereo." },
            { "spectrum:ms",  "Spettro Mid/Side\nArea azzurra = Mid (somma dei canali), linea viola chiaro = Side (differenza). "
                              "Più il Side si avvicina al Mid, più quella zona è larga. Sotto 120 Hz le colonne arancio indicano "
                              "bassi troppo larghi (Side a meno di 6 dB dal Mid): in mono perdono corpo e sul vinile sono un problema. "
                              "Il tratteggio viola è il Side del reference, portato al livello del master." },
            { "spectrum",     "Spettro\nArea azzurra = media long-term del master, linea chiara = spettro istantaneo. Tratteggio "
                              "giallo = curva target, linea viola = reference, tratteggio blu con nodi numerati = EQ suggerito "
                              "(scala propria, ±6 dB), puntinato grigio = versione salvata. Col mouse sopra leggi frequenza, livello e "
                              "scostamento dal target." },
            { "rumble",       "Energia sotto 30 Hz (dB)\nRumble sub-sonico rispetto al totale: consuma headroom e lavoro del "
                              "limiter senza essere udibile. Si toglie con un high-pass a 20-30 Hz." },
            { "tilt",         "Tilt spettrale (dB/oct)\nPendenza media dello spettro: più negativa = suono più scuro e caldo, "
                              "più vicina a zero = più brillante. Confrontala con il target o con il reference." },
            { "centroid",     "Centroide spettrale (Hz)\nIl baricentro delle frequenze: più è alto, più il suono è brillante." },

            // --- stereo -------------------------------------------------------------------------------------
            { "goniometer",   "Goniometro\nMostra il campo stereo. Traccia verticale = mono (tutto al centro); nuvola larga = "
                              "stereo ampio; tendenza orizzontale = contenuto in controfase, che si annulla in mono." },
            { "correlation",  "Correlazione di fase (-1 .. +1)\n+1 = mono, 0 = canali scorrelati (stereo molto largo), sotto 0 "
                              "= controfase: in mono parti del mix si cancellano. La barra è il valore istantaneo, la tacca chiara "
                              "la media, quella viola il reference." },
            { "width",        "Larghezza stereo (%)\nQuota di energia nel canale Side rispetto a Mid + Side: 0% = mono, 50% = "
                              "canali completamente scorrelati. Confrontala con il reference." },
            { "balance",      "Bilanciamento L/R (dB)\nDifferenza di livello tra canale sinistro e destro. Lontano da 0 il mix "
                              "pende da un lato." },
            { "monoLoss",     "Perdita in mono (dB)\nDi quanto scende il livello sommando i canali in mono. Valori alti = il "
                              "mix perde corpo su smartphone, altoparlanti mono e in alcuni club." },
            { "lowEndWidth",  "Side sotto 100 Hz\nLarghezza e correlazione (c) delle basse frequenze. Sub e basso dovrebbero "
                              "essere quasi mono (side basso, c vicina a +1) per impatto, stabilità e compatibilità." },

            // --- streaming ----------------------------------------------------------------------------------
            { "streaming:platform", "Piattaforma\nServizi di streaming con i loro valori di normalizzazione di default." },
            { "streaming:target",   "Target della piattaforma (LUFS)\nLoudness integrata a cui la piattaforma riproduce i brani." },
            { "streaming:gain",     "Guadagno applicato (dB)\nQuanto la piattaforma cambierà il volume del brano. Negativo = lo "
                                    "abbassa perché è più forte del target: la loudness in più non dà vantaggi e costa dinamica. "
                                    "Positivo = lo alza, solo dove la piattaforma lo fa e il true peak lo consente." },
            { "streaming:playback", "In riproduzione (LUFS)\nLoudness a cui il brano verrà ascoltato dopo la normalizzazione." },
            { "streaming:tp",       "TP risultante (dBTP)\nTrue peak del brano dopo il guadagno della piattaforma." },

            // --- report -------------------------------------------------------------------------------------
            { "score",        "Punteggio (0-100)\nMedia dei voti delle cinque aree della pagella (il tonale pesa un po' di più, "
                              "lo stereo un po' meno). Con un problema tecnico critico (clip, fase invertita, canale muto) non supera "
                              "60. Indicativo: serve a seguire i progressi tra una correzione e l'altra, non è un giudizio artistico." },
            { "counters",     "Conteggio diagnosi\nQuante misure sono critiche (rosso), da verificare (giallo) e nel range del "
                              "target (verde), più quelle che hai ignorato come scelte volute." },
            // --- coerenza album -----------------------------------------------------------------------------
            { "panel:album",      "Coerenza album\nOgni brano confrontato con il resto dell'album (la mediana dei brani): loudness "
                                  "della sezione più forte, bilanciamento tonale, ceiling e larghezza stereo. Serve a far suonare "
                                  "l'album come un insieme quando i brani vengono ascoltati in sequenza." },
            { "album:summary",    "Riepilogo dell'album\nQuanti brani escono dall'album, la loudness mediana della sezione più forte "
                                  "e la differenza tra il brano più forte e il più piano. Il confronto tonale usa i ritornelli "
                                  "(sezione più forte) quando tutti i brani ne hanno una." },
            { "album:row",        "Brani\nUna riga per brano, nell'ordine dei nomi dei file. Clic per vedere le sue diagnosi a destra, "
                                  "doppio clic per aprirlo nella dashboard come file master (analisi completa con il target attivo)." },
            { "album:median",     "Mediana dell'album\nIl riferimento comune: il valore centrale tra i brani. Al contrario della media "
                                  "non si sposta per un brano fuori posto, che così risulta segnalato da solo." },
            { "album:integrated", "Integrated (LUFS)\nLoudness media del brano intero. Solo informativa: ballate e brani con più dinamica "
                                  "hanno di solito un'integrated più bassa anche in un album ben bilanciato." },
            { "album:loudest",    "Sezione più forte (LUFS)\nMax short-term: la loudness del ritornello o del drop. In mastering si "
                                  "bilanciano le parti più forti dei brani, perché è lì che l'orecchio confronta un brano con l'altro." },
            { "album:delta",      "Scarto dall'album (LU)\nSezione più forte meno la mediana dell'album. Oltre ±1,5 LU è da verificare, "
                                  "oltre ±3 LU è critico." },
            { "album:gain",       "Guadagno suggerito (dB)\nQuanto alzare o abbassare il brano per portare la sezione più forte sulla "
                                  "mediana. Compare solo per i brani fuori tolleranza: se il true peak non lascia spazio, si alza "
                                  "spingendo il limiter." },
            { "album:tp",         "True peak max (dBTP)\nIn azzurro se è diverso di oltre 1 dB dalla mediana: di solito un album usa "
                                  "lo stesso ceiling del limiter per tutti i brani." },
            { "album:width",      "Larghezza stereo (%)\nIn azzurro se si discosta di oltre 8 punti dalla mediana: un brano molto più "
                                  "largo o stretto degli altri si nota nel passaggio da uno all'altro." },
            { "album:status",     "Stato del brano\nLa diagnosi più grave: verde in linea con l'album, giallo da verificare, rosso "
                                  "critico, azzurro solo note informative." },
            { "album:details",    "Diagnosi del brano\nLe differenze dal resto dell'album con la correzione suggerita: guadagno per "
                                  "la loudness, shelf o campana larga per il bilanciamento tonale." },
            { "album:tonalChart", "Tonale rispetto all'album\nScostamento di ogni banda dalla curva mediana dell'album, in dB (±6 dB "
                                  "a fondo scala). La fascia verde è la tolleranza: più larga su Sub e Air, che variano di più "
                                  "tra brani." },

            { "report:list","Diagnosi\nOgni scheda riporta valore, target, la spiegazione e cosa fare. In cima \"Da fare prima\": "
                              "al massimo 3 interventi nell'ordine in cui conviene affrontarli. Clic destro su una diagnosi per "
                              "ignorarla quando è una scelta voluta: esce da voti e priorità e finisce in fondo, tra le ignorate." },
        };
        return map;
    }

    /** Contenuto tipico di ogni banda tonale (stesso ordine di kBandNames). */
    const char* bandContent (int b)
    {
        static const char* content[] = {
            "cassa e sub-bass: si sente più nel corpo che nelle orecchie",
            "corpo di basso e cassa, fondamentali delle note basse",
            "calore; in eccesso il mix diventa confuso e fangoso",
            "corpo di voci e strumenti; in eccesso suona nasale o \"in scatola\"",
            "presenza e intelligibilità; in eccesso diventa aspro",
            "definizione e attacco; in eccesso risulta affaticante",
            "brillantezza e sibilanti",
            "aria e apertura",
        };
        return juce::isPositiveAndBelow (b, kNumBands) ? content[b] : "";
    }

    juce::String bandTitle (int b)
    {
        auto hz = [] (float f) { return f >= 1000.0f ? juce::String (f / 1000.0f, 0) + "k" : juce::String (juce::roundToInt (f)); };
        return juce::String (kBandNames[(size_t) b]) + " (" + hz (kBandEdges[(size_t) b]) + "-" + hz (kBandEdges[(size_t) b + 1]) + " Hz)";
    }
}

juce::String helpTextFor (const juce::String& key)
{
    if (key.isEmpty())
        return {};

    if (const auto it = texts().find (key); it != texts().end())
        return juce::String::fromUTF8 (it->second);

    // chiavi con indice o valore
    const auto prefix = key.upToFirstOccurrenceOf (":", false, false);
    const auto arg = key.fromFirstOccurrenceOf (":", false, false);
    const int index = arg.getIntValue();

    if (prefix == "band" && juce::isPositiveAndBelow (index, kNumBands))
        return bandTitle (index) + "\nScostamento dell'energia della banda dal target, in dB: + sopra, - sotto. La fascia verde "_u
               "è la tolleranza. Contenuto tipico: "_u + juce::String::fromUTF8 (bandContent (index)) + ".";

    if (key.startsWith ("album:band:"))
        if (const int b = key.getTrailingIntValue(); juce::isPositiveAndBelow (b, kNumBands))
            return bandTitle (b) + "\nScostamento della banda dalla curva mediana dell'album, in dB. Colorata quando supera la "_u
                   "tolleranza (almeno 1,5 dB, di più su Sub e Air). Contenuto tipico: "_u + juce::String::fromUTF8 (bandContent (b)) + ".";

    if (prefix == "bandWidth" &&juce::isPositiveAndBelow (index, kNumBands))
        return "Larghezza " + bandTitle (index) + "\nPercentuale di Side nella banda (barra) e del reference (linea viola). "
               "Sub e Bass dovrebbero restare quasi mono.";

    if (prefix == "bandCorr" && juce::isPositiveAndBelow (index, kNumBands))
        return "Correlazione " + bandTitle (index) + "\nCorrelazione di fase della banda: sotto 0 c'è controfase; nelle basse "_u
               "già sotto 0,7 è un campanello d'allarme per la compatibilità mono."_u;

    if (prefix == "crest" && juce::isPositiveAndBelow (index, kNumCrestBands))
        return "Crest " + juce::String (kCrestBandNames[(size_t) index]) + "\nQuanto i transienti emergono sopra il livello medio in "
               "questa zona di frequenze. Basso = banda molto compressa o saturata; alto = transienti marcati."_u;

    if (prefix == "area" && juce::isPositiveAndBelow (index, kNumAreas))
    {
        static const char* content[] = {
            "integrated, true peak, LRA ed eventi oltre 0 dBTP",
            "PLR, PSR, DR e crest factor per banda",
            "bilanciamento delle bande, tilt, risonanze",
            "correlazione, larghezza, low-end mono, compatibilità mono",
            "clipping, DC offset, canali muti, fase invertita",
        };
        return areaName ((Area) index) + "\n" + "Voto dell'area (0-100): ogni misura valutata pesa allo stesso modo, una critica "_u
               "pesa due volte e mezzo una da verificare. Comprende: "_u + juce::String::fromUTF8 (content[index]) + ". "
               "Clic per vedere solo le diagnosi di quest'area."_u;
    }

    if (prefix == "eq")
        return "Mossa EQ " + juce::String (index + 1) + "\nIntervento suggerito: tipo, frequenza, guadagno e Q sono nel report. "
               "La curva tratteggiata somma tutte le mosse (scala ±6 dB)."_u;

    if (prefix == "resonance")
        return "Picco stretto a " + arg + " Hz\nPicco persistente nello spettro. In grigio con il nome della nota è una nota "_u
               "portante del brano (tonica, basso): non va corretta. In arancio è una risonanza da attenuare con un EQ stretto."_u;

    return {};
}

} // namespace ma::ui
