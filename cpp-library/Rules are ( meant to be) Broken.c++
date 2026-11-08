// Rulebreaker v3 (ελληνική έκδοση) - ένα μικρό roguelike που αλλάζει μόνο του τους κανόνες του.
// Μεταγλώττιση:  g++ -std=c++17 -O2 rulebreaker_el.cpp -o rulebreaker
// Παιχνίδι:      γράψε w/a/s/d (ή q για έξοδο) και πάτα Enter.
//                (Χρησιμοποίησε αγγλική διάταξη πληκτρολογίου. Στα Windows γράψε πρώτα: chcp 65001)
//
// ΣΤΟΧΟΣ: Σε κάθε όροφο μάζεψε ΟΛΑ τα παστέλια (+) για να ανοίξει η έξοδος, μετά φτάσε
//         στην ανοιχτή έξοδο (>). Επιβίωσε FINAL_FLOOR ορόφους για να κερδίσεις.
//         Πέσε πάνω σε 'E' για να τον σκοτώσεις. Το δηλητήριο (-) πονάει.
//         Η σφραγισμένη έξοδος είναι το X.
//         Κάθε λίγους γύρους το παιχνίδι αλλάζει έναν από τους κανόνες του.

#include <algorithm>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>
using namespace std;

struct Pos { int x, y; };
struct Enemy { Pos p; };

struct Game;

// ---------- Διεπαφή κανόνα: κάθε κανόνας είναι ένα εναλλάξιμο αντικείμενο ----------
struct Rule {
    virtual ~Rule() = default;
    virtual string name() const = 0;
    virtual string blurb() const = 0;

    virtual void onActivate(Game&) {}                          // ο κανόνας ξεκινά
    virtual void onDeactivate(Game&) {}                        // ο κανόνας τελειώνει
    virtual char remapInput(char c) const { return c; }        // αλλαγή του πλήκτρου που πατάς
    virtual bool moveEnemy(Game&, Enemy&) { return false; }    // αλλαγή κίνησης εχθρών (true = το χειρίστηκα)
    virtual void afterTurn(Game&) {}                           // επιδράσεις στον κόσμο μετά τον γύρο
    virtual int visionRadius() const { return 99; }            // πόσο μακριά βλέπει ο παίκτης
    virtual int modifyItemEffect(int hpDelta) const { return hpDelta; }  // αλλαγή του αποτελέσματος αντικειμένων
    virtual string activateMessage() const { return ""; }      // μήνυμα που εμφανίζεται όταν ξεκινά ο κανόνας
};

unique_ptr<Rule> makeRandomRule(mt19937& rng, const vector<unique_ptr<Rule>>& active);

// ---------- Κατάσταση παιχνιδιού ----------
struct Game {
    static constexpr int RULE_INTERVAL = 6;   // γύροι ανάμεσα στις αλλαγές κανόνα
    static constexpr int MAX_RULES = 2;       // ενεργοί κανόνες ταυτόχρονα
    static constexpr int MAX_HP = 5;          // μέγιστη ζωή
    static constexpr int FINAL_FLOOR = 8;     // πόσους ορόφους πρέπει να περάσεις για να κερδίσεις

    int W = 24, H = 10;                       // το μέγεθος του ορόφου μεγαλώνει με το βάθος
    int floorNo = 1;
    int pasteliaCollected = 0, pasteliaNeeded = 0, pasteliaTotal = 0;  // παστέλια: στον όροφο / που χρειάζονται / σε όλο το παιχνίδι
    int baseEnemies = 4;

    vector<string> map;
    Pos player{1, 1}, exitPos{22, 8};
    vector<Enemy> enemies;
    vector<unique_ptr<Rule>> rules;
    unique_ptr<Rule> nextRule;                // ο κανόνας που φαίνεται από πριν και ενεργοποιείται στην επόμενη αλλαγή
    int hp = 3, turn = 0;
    string message;
    mt19937 rng{random_device{}()};

    bool inBounds(Pos p) const { return p.x >= 0 && p.x < W && p.y >= 0 && p.y < H; }
    bool walkable(Pos p) const { return inBounds(p) && map[p.y][p.x] != '#'; }
    bool isPlayer(Pos p) const { return p.x == player.x && p.y == player.y; }
    int enemyIndexAt(Pos p) const {
        for (size_t i = 0; i < enemies.size(); ++i)
            if (enemies[i].p.x == p.x && enemies[i].p.y == p.y) return (int)i;
        return -1;
    }

    // γέμισμα από τη θέση του παίκτη: σε ποια τετράγωνα μπορεί να φτάσει;
    vector<vector<bool>> reachable() const {
        vector<vector<bool>> seen(H, vector<bool>(W, false));
        vector<Pos> q{player};
        seen[player.y][player.x] = true;
        static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (size_t i = 0; i < q.size(); ++i)
            for (int k = 0; k < 4; ++k) {
                Pos n{q[i].x + dx[k], q[i].y + dy[k]};
                if (walkable(n) && !seen[n.y][n.x]) { seen[n.y][n.x] = true; q.push_back(n); }
            }
        return seen;
    }

    // φτιάχνει καινούργιο όροφο για το floorNo (οι κανόνες και ο μετρητής γύρων συνεχίζουν)
    void generateFloor() {
        W = min(40, 22 + 2 * floorNo);
        H = min(16, 9 + floorNo);
        player = {1, 1};
        exitPos = {W - 2, H - 2};
        enemies.clear();
        pasteliaCollected = 0;
        pasteliaNeeded = min(8, 2 + floorNo);
        baseEnemies = min(12, 3 + floorNo);
        int poisons = 2 + floorNo / 2;

        uniform_int_distribution<int> rx(1, W - 2), ry(1, H - 2);
        vector<vector<bool>> reach;
        do {  // ξαναφτιάχνουμε τον όροφο μέχρι να μπορεί να φτάσει κανείς στην έξοδο
            map.assign(H, string(W, '.'));
            for (int x = 0; x < W; ++x) map[0][x] = map[H - 1][x] = '#';
            for (int y = 0; y < H; ++y) map[y][0] = map[y][W - 1] = '#';
            for (int i = 0; i < W * H / 10; ++i) map[ry(rng)][rx(rng)] = '#';
            map[player.y][player.x] = '.';
            map[exitPos.y][exitPos.x] = 'X';   // σφραγισμένη μέχρι να μαζευτούν όλα τα παστέλια
            reach = reachable();
        } while (!reach[exitPos.y][exitPos.x]);

        // παστέλια και δηλητήριο μπαίνουν μόνο σε τετράγωνα που ο παίκτης μπορεί να φτάσει
        vector<Pos> spots;
        for (int y = 1; y < H - 1; ++y)
            for (int x = 1; x < W - 1; ++x)
                if (reach[y][x] && map[y][x] == '.' && !(x == player.x && y == player.y))
                    spots.push_back({x, y});
        shuffle(spots.begin(), spots.end(), rng);
        size_t idx = 0;
        for (int i = 0; i < pasteliaNeeded && idx < spots.size(); ++i, ++idx)
            map[spots[idx].y][spots[idx].x] = '+';
        for (int i = 0; i < poisons && idx < spots.size(); ++i, ++idx)
            map[spots[idx].y][spots[idx].x] = '-';

        for (int tries = 0; (int)enemies.size() < baseEnemies && tries < 2000; ++tries) {
            Pos p{rx(rng), ry(rng)};
            if (map[p.y][p.x] == '.' && !isPlayer(p) && enemyIndexAt(p) < 0
                && abs(p.x - player.x) + abs(p.y - player.y) > 6)
                enemies.push_back({p});
        }
    }

    void init() {
        generateFloor();
        rules.push_back(makeRandomRule(rng, rules));
        rules.back()->onActivate(*this);
        nextRule = makeRandomRule(rng, rules);
        message = "Όροφος 1: μάζεψε και τα " + to_string(pasteliaNeeded) + " παστέλια (+), μετά φτάσε στην έξοδο. Πρώτος κανόνας: "
                  + rules.back()->name() + " - " + rules.back()->blurb() + " " + rules.back()->activateMessage();
    }

    int visionRadius() const {
        int v = 99;
        for (auto& r : rules) v = min(v, r->visionRadius());
        return v;
    }

    void draw() const {
        int vis = visionRadius();
        cout << "\n";
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                Pos p{x, y};
                if (max(abs(x - player.x), abs(y - player.y)) > vis) cout << ' ';
                else if (isPlayer(p)) cout << '@';
                else if (enemyIndexAt(p) >= 0) cout << 'E';
                else cout << map[y][x];
            }
            cout << "\n";
        }
        cout << "Όροφος " << floorNo << "/" << FINAL_FLOOR << "   Παστέλια: " << pasteliaCollected << "/" << pasteliaNeeded
             << "   Ζωή: " << hp << "/" << MAX_HP << "   Γύρος: " << turn << "\nΕνεργοί κανόνες: ";
        for (auto& r : rules) cout << "[" << r->name() << "] ";
        int left = RULE_INTERVAL - (turn % RULE_INTERVAL);
        cout << "\nΕπόμενος κανόνας σε " << left << (left == 1 ? " γύρο" : " γύρους") << ": "
             << nextRule->name() << " (" << nextRule->blurb() << ")\n";
        if (!message.empty()) cout << ">>" << message << "\n";
    }

    // προεπιλεγμένη συμπεριφορά εχθρού: κάνει ένα βήμα προς τον παίκτη
    void defaultEnemyMove(Enemy& e) {
        int dx = (player.x > e.p.x) - (player.x < e.p.x);
        int dy = (player.y > e.p.y) - (player.y < e.p.y);
        Pos options[2] = {{e.p.x + dx, e.p.y}, {e.p.x, e.p.y + dy}};
        if (rng() % 2) swap(options[0], options[1]);
        for (Pos o : options)
            if ((o.x != e.p.x || o.y != e.p.y) && walkable(o) && enemyIndexAt(o) < 0) { e.p = o; return; }
    }

    void unlockExit() {
        for (auto& row : map) replace(row.begin(), row.end(), 'X', '>');
        message += " Η έξοδος άνοιξε!";
    }

    void pickUpItem() {
        char& c = map[player.y][player.x];
        if (c != '+' && c != '-') return;
        bool pasteli = (c == '+');
        int delta = pasteli ? 1 : -1;
        for (auto& r : rules) delta = r->modifyItemEffect(delta);  // οι κανόνες μπορεί να αντιστρέψουν το αποτέλεσμα
        hp = min(MAX_HP, hp + delta);
        c = '.';
        if (pasteli) {
            ++pasteliaCollected; ++pasteliaTotal;
            message += " Παστέλι " + to_string(pasteliaCollected) + "/" + to_string(pasteliaNeeded)
                       + (delta > 0 ? " (+1 ζωή)." : " - αλλά πονάει! (-1 ζωή).");
            if (pasteliaCollected == pasteliaNeeded) unlockExit();
        } else {
            message += (delta < 0) ? " Δηλητήριο! (-1 ζωή)." : " Περίεργο - το δηλητήριο σε γιατρεύει (+1 ζωή).";
        }
    }

    // 0 = τίποτα, 1 = κατέβηκες σε νέο όροφο, 2 = βγήκες από τον τελευταίο όροφο
    int checkExit() {
        char c = map[player.y][player.x];
        if (c == 'X') { message += " Η έξοδος είναι σφραγισμένη - μάζεψε πρώτα όλα τα παστέλια."; return 0; }
        if (c != '>') return 0;
        if (floorNo >= FINAL_FLOOR) return 2;
        ++floorNo;
        hp = min(MAX_HP, hp + 1);
        generateFloor();
        message = " Κατεβαίνεις στον όροφο " + to_string(floorNo) + " (+1 ζωή). Μάζεψε " + to_string(pasteliaNeeded)
                  + " παστέλια και βρες την έξοδο.";
        return 1;
    }

    bool die() {
        draw();
        cout << "\nΠέθανες στον όροφο " << floorNo << " έχοντας μαζέψει " << pasteliaTotal << " παστέλια.\n";
        return false;
    }
    bool win() {
        draw();
        cout << "\nΞέφυγες και από τους " << FINAL_FLOOR << " ορόφους με " << pasteliaTotal << " παστέλια. Κέρδισες!\n";
        return false;
    }

    void rotateRules() {
        if ((int)rules.size() >= MAX_RULES) {
            rules.front()->onDeactivate(*this);   // ο παλαιότερος κανόνας λήγει
            rules.erase(rules.begin());
        }
        rules.push_back(move(nextRule));          // ο κανόνας που φαινόταν γίνεται ενεργός
        rules.back()->onActivate(*this);
        nextRule = makeRandomRule(rng, rules);    // επιλέγουμε τον επόμενο για προεπισκόπηση
        message += " ΑΛΛΑΓΗ ΚΑΝΟΝΑ! " + rules.back()->name() + " - " + rules.back()->blurb() + " " + rules.back()->activateMessage();
    }

    // επιστρέφει false όταν τελειώσει το παιχνίδι
    bool playTurn(char c) {
        message.clear();
        for (auto& r : rules) c = r->remapInput(c);

        Pos d{0, 0};
        if (c == 'w') d.y = -1; else if (c == 's') d.y = 1;
        else if (c == 'a') d.x = -1; else if (c == 'd') d.x = 1;
        Pos np{player.x + d.x, player.y + d.y};

        int hit = enemyIndexAt(np);
        if (hit >= 0) { enemies.erase(enemies.begin() + hit); message += " Τσάκισες έναν εχθρό!"; }
        else if (walkable(np)) player = np;
        pickUpItem();
        if (hp <= 0) return die();
        int ex = checkExit();
        if (ex == 2) return win();
        if (ex == 1) return true;                 // νέος όροφος, οι εχθροί δεν κινούνται αυτόν τον γύρο

        for (auto& e : enemies) {
            bool handled = false;
            for (auto& r : rules) if (r->moveEnemy(*this, e)) { handled = true; break; }
            if (!handled) defaultEnemyMove(e);
        }
        for (auto& r : rules) r->afterTurn(*this);
        pickUpItem();                             // ο αέρας μπορεί να σε έσπρωξε πάνω σε αντικείμενο
        if (hp <= 0) return die();
        ex = checkExit();                         // ...ή πάνω στην έξοδο
        if (ex == 2) return win();
        if (ex == 1) return true;

        int touch = enemyIndexAt(player);
        while (touch >= 0) {
            --hp; enemies.erase(enemies.begin() + touch);
            message += " Ένας εχθρός σε χτύπησε!";
            touch = enemyIndexAt(player);
        }
        if (hp <= 0) return die();

        if (++turn % RULE_INTERVAL == 0) rotateRules();
        return true;
    }
};

// ---------- Συγκεκριμένοι κανόνες ----------
struct ReversedControls : Rule {
    string name() const override { return "Ανάποδα Χειριστήρια"; }
    string blurb() const override { return "το πάνω είναι κάτω, το αριστερά είναι δεξιά"; }
    char remapInput(char c) const override {
        switch (c) { case 'w': return 's'; case 's': return 'w'; case 'a': return 'd'; case 'd': return 'a'; }
        return c;
    }
};

struct FleeingEnemies : Rule {
    string name() const override { return "Δειλοί"; }
    string blurb() const override { return "οι εχθροί τρέχουν μακριά σου"; }
    bool moveEnemy(Game& g, Enemy& e) override {
        int dx = (e.p.x > g.player.x) - (e.p.x < g.player.x);
        int dy = (e.p.y > g.player.y) - (e.p.y < g.player.y);
        Pos options[2] = {{e.p.x + dx, e.p.y}, {e.p.x, e.p.y + dy}};
        for (Pos o : options)
            if ((o.x != e.p.x || o.y != e.p.y) && g.walkable(o) && g.enemyIndexAt(o) < 0) { e.p = o; break; }
        return true;
    }
};

struct DrunkenEnemies : Rule {
    string name() const override { return "Μεθυσμένοι Εχθροί"; }
    string blurb() const override { return "οι εχθροί παραπαίουν τυχαία"; }
    bool moveEnemy(Game& g, Enemy& e) override {
        static const Pos dirs[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        Pos d = dirs[g.rng() % 4];
        Pos np{e.p.x + d.x, e.p.y + d.y};
        if (g.walkable(np) && g.enemyIndexAt(np) < 0) e.p = np;
        return true;
    }
};

// Χρησιμοποιεί το onActivate: η κατεύθυνση του αέρα επιλέγεται όταν ξεκινά ο κανόνας.
struct StrongWind : Rule {
    Pos d{1, 0};
    string dirName = "μια τυχαία κατεύθυνση";
    bool active = false;
    string name() const override { return "Δυνατός Αέρας"; }
    string blurb() const override { return "σε σπρώχνει ο αέρας προς " + dirName + " κάθε γύρο"; }
    void onActivate(Game& g) override {
        static const Pos ds[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        static const char* names[4] = {"ανατολικά", "δυτικά", "νότια", "βόρεια"};
        int i = g.rng() % 4;
        d = ds[i]; dirName = names[i]; active = true;
    }
    void afterTurn(Game& g) override {
        if (!active) return;
        Pos np{g.player.x + d.x, g.player.y + d.y};
        if (g.walkable(np)) g.player = np;
    }
};

struct Fog : Rule {
    string name() const override { return "Πυκνή Ομίχλη"; }
    string blurb() const override { return "βλέπεις μόνο 3 τετράγωνα γύρω σου"; }
    int visionRadius() const override { return 3; }
    string activateMessage() const override { return "We have 0 visibility - visibility is limited to 3 tiles around you!"; }
};

struct ShiftingWalls : Rule {
    string name() const override { return "Κινούμενοι Τοίχοι"; }
    string blurb() const override { return "οι τοίχοι γλιστρούν κάθε γύρο"; }
    void afterTurn(Game& g) override {
        static const Pos dirs[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (int moved = 0, tries = 0; moved < 3 && tries < 60; ++tries) {
            int x = 1 + g.rng() % (g.W - 2), y = 1 + g.rng() % (g.H - 2);
            if (g.map[y][x] != '#') continue;
            Pos d = dirs[g.rng() % 4];
            Pos np{x + d.x, y + d.y};
            bool interior = np.x >= 1 && np.x <= g.W - 2 && np.y >= 1 && np.y <= g.H - 2;
            if (interior && g.map[np.y][np.x] == '.' && !g.isPlayer(np) && g.enemyIndexAt(np) < 0) {
                g.map[y][x] = '.';
                g.map[np.y][np.x] = '#';
                ++moved;
            }
        }
    }
};

// Χρησιμοποιεί το onDeactivate: το σμήνος λιγοστεύει όταν τελειώνει ο κανόνας.
struct Breeding : Rule {
    string name() const override { return "Αναπαραγωγή"; }
    string blurb() const override { return "οι εχθροί διπλασιάζονται κάθε δεύτερο γύρο"; }
    void afterTurn(Game& g) override {
        if (g.turn % 2 != 0) return;
        static const Pos dirs[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        size_t n = g.enemies.size();
        for (size_t i = 0; i < n && g.enemies.size() < 16; ++i) {
            if (g.rng() % 100 >= 30) continue;
            Pos base = g.enemies[i].p;
            Pos d = dirs[g.rng() % 4];
            Pos np{base.x + d.x, base.y + d.y};
            if (g.walkable(np) && g.enemyIndexAt(np) < 0 && !g.isPlayer(np)) g.enemies.push_back({np});
        }
    }
    void onDeactivate(Game& g) override {
        while ((int)g.enemies.size() > g.baseEnemies) g.enemies.pop_back();
        g.message += " Το σμήνος λιγοστεύει.";
    }
};

// Τα παστέλια πονάνε και το δηλητήριο γιατρεύει - αλλά τα παστέλια μετράνε κανονικά στην ποσόστωση.
struct MixedUpItems : Rule {
    string name() const override { return "Μπερδεμένα Αντικείμενα"; }
    string blurb() const override { return "τα παστέλια πονάνε, το δηλητήριο γιατρεύει"; }
    int modifyItemEffect(int hpDelta) const override { return -hpDelta; }
};

unique_ptr<Rule> makeRandomRule(mt19937& rng, const vector<unique_ptr<Rule>>& active) {
    while (true) {
        unique_ptr<Rule> r;
        switch (rng() % 8) {
            case 0: r = make_unique<ReversedControls>(); break;
            case 1: r = make_unique<FleeingEnemies>(); break;
            case 2: r = make_unique<DrunkenEnemies>(); break;
            case 3: r = make_unique<StrongWind>(); break;
            case 4: r = make_unique<Fog>(); break;
            case 5: r = make_unique<ShiftingWalls>(); break;
            case 6: r = make_unique<Breeding>(); break;
            default: r = make_unique<MixedUpItems>(); break;
        }
        bool dup = any_of(active.begin(), active.end(),
                          [&](const unique_ptr<Rule>& a) { return a->name() == r->name(); });
        if (!dup) return r;
    }
}

int main() {
    Game g;
    g.init();
    cout << "RULEBREAKER  (@ εσύ, E εχθρός, + παστέλι, - δηλητήριο, X σφραγισμένη έξοδος, > ανοιχτή έξοδος)\n";
    string line;
    while (true) {
        g.draw();
        cout << "κίνηση (w/a/s/d, q για έξοδο): ";
        if (!getline(cin, line)) break;
        if (line.empty()) continue;
        if (line[0] == 'q') break;
        if (!g.playTurn(line[0])) break;
    }
    return 0;
}