#pragma once

// Banco di prova dell'accuratezza: difetti noti inseriti in memoria in master commerciali, poi analisi e
// confronto con il profilo del genere, con il profilo dell'album (senza il brano) e con l'originale come reference.
//   MasterAgentAnalyze --bench <jobs.json> <cartella risultati>
// jobs.json (preparato da tools/bench.py):
//   [ { "id": "...", "file": "...", "genre": "rock", "genreProfile": "profilo.json" (facoltativo),
//       "albumSnapshots": [ "x.snap", ... ], "variants": "all" | "original" } ]
// Per ogni job scrive <cartella>/<id>.json (se esiste già lo salta: il banco si può riprendere).

int runBench (int argc, char* argv[]);

/** Nomi e parametri delle varianti, per controllo: MasterAgentAnalyze --bench-list */
int listBenchVariants();
