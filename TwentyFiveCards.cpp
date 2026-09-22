
#include <algorithm>
#include <iostream>
#include <random>
#include <string>
#include <vector>

// ---------- Card ----------
enum class Suit { Spades, Hearts, Clubs, Diamonds };

struct Card {
    int  rank;   // 1 = Ace ... 12 = Queen, 13 = King (Puppet)
    Suit suit;
    int  deck;   // which physical deck it came from (flavor)

    bool isPuppet() const { return rank == 13; }
};

const char* rankName(int r) {
    static const char* names[] = {
        "", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"
    };
    return (r >= 1 && r <= 13) ? names[r] : "?";
}

const char* suitName(Suit s) {
    switch (s) {
        case Suit::Spades:   return "S";
        case Suit::Hearts:   return "H";
        case Suit::Clubs:    return "C";
        case Suit::Diamonds: return "D";
    }
    return "?";
}

std::string cardName(const Card& c) {
    std::string s = std::string(rankName(c.rank)) + suitName(c.suit);
    if (c.isPuppet()) s += " (PUPPET)";
    return s;
}

// ---------- Player ----------
struct Player {
    std::string name;
    std::vector<Card> hand;
    std::vector<Card> goal;                       // 25-card deck, back() = face-up top
    std::vector<std::vector<Card>> parking;       // 4 slots, back() = top
    bool done = false;

    Player() { parking.resize(4); }

    const Card* goalTop() const { return goal.empty() ? nullptr : &goal.back(); }
};

// ---------- Pack (middle of the table) ----------
struct Pack {
    std::vector<Card> cards;  // the actual cards, in play order (back = top)
    int nextNeeded() const {
        if (cards.empty()) return 1;
        int top = cards.back().rank;
        return (top == 12) ? -1 : top + 1;   // -1 = complete (A..Q)
    }
    bool complete() const { return (int)cards.size() == 12; }
    std::string describe() const {
        std::string s;
        for (size_t i = 0; i < cards.size(); ++i)
            s += (i ? "-" : "") + std::string(rankName(cards[i].rank));
        return s.empty() ? "(empty)" : s;
    }
};

// ---------- Game ----------
struct Game {
    std::vector<Player> players;
    std::vector<Card>   draw;     // shared draw pile
    std::vector<Pack>   packs;    // middle of the table
    int                 turn = 0;
    int                 winner = -1;

    static constexpr int HAND_SIZE = 5;
    static constexpr int GOAL_SIZE = 25;
    static constexpr int NUM_SLOTS = 4;

    void log(const std::string& m) const { std::cout << "  " << m << "\n"; }

    // Can card c be placed on pack p? (Puppet always fits.)
    static bool fits(const Card& c, const Pack& p) {
        if (c.isPuppet()) return !p.complete();
        int need = p.nextNeeded();
        return need > 0 && c.rank == need;
    }

    // ---------- Setup ----------
    static std::vector<Card> makeDeck(int id) {
        std::vector<Card> d;
        d.reserve(52);
        for (Suit s : { Suit::Spades, Suit::Hearts, Suit::Clubs, Suit::Diamonds })
            for (int r = 1; r <= 13; ++r) d.push_back({ r, s, id });
        return d;
    }
    static std::mt19937& rng() {
        static std::mt19937 gen{ std::random_device{}() };
        return gen;
    }

    void start(const std::string& n0, const std::string& n1) {
        players.clear(); draw.clear(); packs.clear(); turn = 0; winner = -1;

        std::vector<Card> all;
        for (int d = 0; d < 2; ++d) {
            auto deck = makeDeck(d);
            std::shuffle(deck.begin(), deck.end(), rng());
            all.insert(all.end(), deck.begin(), deck.end());
        }
        std::shuffle(all.begin(), all.end(), rng());   // 104 cards

        auto take = [&](int n) {
            std::vector<Card> out;
            for (int i = 0; i < n; ++i) { out.push_back(all.back()); all.pop_back(); }
            return out;
        };

        Player p0, p1;
        p0.name = n0; p1.name = n1;
        p0.hand = take(HAND_SIZE);  p1.hand = take(HAND_SIZE);
        p0.goal = take(GOAL_SIZE);  p1.goal = take(GOAL_SIZE);
        draw = std::move(all);      // 49 left

        players = { p0, p1 };

        std::cout << "\n25 CARDS — " << n0 << " vs " << n1 << "\n";
        std::cout << "Draw pile: " << draw.size() << " cards. "
                     "Each goal deck: " << GOAL_SIZE << " (top face-up).\n";
        std::cout << "Build packs A->Q in the middle. Complete packs recycle to the draw pile.\n";
        std::cout << "Empty your goal deck to win. Kings are PUPPETS (any rank).\n";
    }

    // ---------- Actions ----------
    enum Source { HAND, GOAL, SLOT };

    void place(int packIdx, Source src, int idx) {
        Player& p = players[turn];
        Card c;
        if (src == HAND) {
            c = p.hand[idx];
            p.hand.erase(p.hand.begin() + (long)idx);
        } else if (src == GOAL) {
            c = p.goal.back();
            p.goal.pop_back();
        } else {
            c = p.parking[idx].back();
            p.parking[idx].pop_back();
        }

        // Puppet lands as the rank the pack actually needed (remembered on the card).
        if (c.isPuppet()) c.rank = packs[packIdx].nextNeeded();
        packs[packIdx].cards.push_back(c);
        log(p.name + (src == GOAL ? " [goal] " : src == SLOT ? " [slot" + std::to_string(idx + 1) + "] " : " [hand] ")
            + "played " + cardName(c)
            + " -> pack " + std::to_string(packIdx + 1) + " (" + packs[packIdx].describe() + ")");

        // Recycle any completed packs back into the draw pile.
        recycleCompleted();

        if (p.goal.empty()) {
            p.done = true;
            winner = turn;
            log("*** " + p.name + " emptied their goal deck and WINS! ***");
            return;
        }
        // A play does NOT end the turn — the player keeps playing until they park.
        // If the hand is now empty but a move still exists, draw 5 again and continue.
        if (p.hand.empty() && hasMove(p)) {
            int drew = HAND_SIZE - (int)p.hand.size();
            refill(p);
            if (drew > 0)
                log(p.name + " hand empty but moves remain — drew " + std::to_string(drew) + " more (hand=" +
                    std::to_string(p.hand.size()) + ").");
        }
    }

    // Does this player have any legal play right now (goal-top, hand, or slot-top)?
    bool hasMove(const Player& p) const {
        if (p.goalTop())
            for (const auto& pk : packs) if (fits(*p.goalTop(), pk)) return true;
        for (const auto& c : p.hand)
            for (const auto& pk : packs) if (fits(c, pk)) return true;
        for (const auto& slot : p.parking)
            if (!slot.empty())
                for (const auto& pk : packs) if (fits(slot.back(), pk)) return true;
        return false;
    }

    void recycleCompleted() {
        std::vector<Pack> remaining;
        int recycled = 0;
        for (auto& pk : packs) {
            if (pk.complete()) {
                // The pack's ACTUAL cards (Puppets included, as played) go back,
                // shuffled, to the BOTTOM of the draw pile.
                std::shuffle(pk.cards.begin(), pk.cards.end(), rng());
                for (auto& c : pk.cards) draw.push_back(c);
                ++recycled;
            } else {
                remaining.push_back(std::move(pk));
            }
        }
        packs = std::move(remaining);
        if (recycled)
            log("  ** " + std::to_string(recycled) + " pack(s) complete — shuffled to the bottom of the draw pile ("
                + std::to_string(draw.size()) + " cards).");
    }

    void refill(Player& p) {
        while ((int)p.hand.size() < HAND_SIZE && !draw.empty()) {
            Card c = draw.back();
            draw.pop_back();
            p.hand.push_back(c);
        }
    }

    // Parking is the ONLY way to end a turn. One card, then pass.
    void park(int slot, int handIdx) {
        Player& p = players[turn];
        Card c = p.hand[handIdx];
        p.hand.erase(p.hand.begin() + (long)handIdx);
        p.parking[slot].push_back(c);
        log(p.name + " parked " + cardName(c) + " in slot " + std::to_string(slot + 1)
            + " (stack of " + std::to_string(p.parking[slot].size()) + ") — turn ends.");
        advanceTurn();
    }

    void advanceTurn() {
        for (int step = 0; step < (int)players.size(); ++step) {
            turn = (turn + 1) % (int)players.size();
            if (!players[turn].done) break;
        }
        beginTurn();
    }

    // Start of a turn: draw from the shared pile until the hand is back to 5.
    void beginTurn() {
        Player& p = players[turn];
        int drew = HAND_SIZE - (int)p.hand.size();
        refill(p);
        if (drew > 0)
            log(p.name + " drew " + std::to_string(drew) + " card(s) to refill (hand=" +
                std::to_string(p.hand.size()) + ").");
    }

    // ---------- Display ----------
    // perspective: whose hand/parking to reveal. The opponent's hand and
    // parking are hidden (face-down); only the shared table (packs, draw
    // count) and BOTH goal-deck tops (face-up by rule) are always visible.
    void printState(int perspective) const {
        std::cout << "\n  DRAW pile: " << draw.size() << " cards\n";
        std::cout << "  PACKS (middle):";
        if (packs.empty()) std::cout << "  (none — play an Ace to start one)";
        for (size_t i = 0; i < packs.size(); ++i)
            std::cout << "  [" << (i + 1) << "] " << packs[i].describe()
                      << (packs[i].cards.empty() ? "" : "  needs " + std::string(rankName(packs[i].nextNeeded())));
        std::cout << "\n";

        for (size_t i = 0; i < players.size(); ++i) {
            const Player& p = players[i];
            bool mine = ((int)i == perspective);
            std::cout << "  " << ((int)i == turn && !p.done ? "> " : "  ") << p.name;
            if (p.done) { std::cout << "  [WON]\n"; continue; }
            // goal-deck top is ALWAYS face-up (shared info)
            std::cout << "\n      goal [" << p.goal.size() << "]: top="
                      << (p.goalTop() ? cardName(*p.goalTop()) : std::string("—"));
            if (mine) {
                std::cout << "\n      hand [" << p.hand.size() << "]: ";
                for (size_t j = 0; j < p.hand.size(); ++j)
                    std::cout << (j ? "  " : "") << cardName(p.hand[j]);
                std::cout << "\n      parking:";
                for (int s = 0; s < NUM_SLOTS; ++s) {
                    std::cout << " [" << (s + 1) << "] ";
                    const auto& slot = p.parking[s];
                    if (slot.empty()) { std::cout << "-"; continue; }
                    for (size_t j = 0; j < slot.size(); ++j)
                        std::cout << (j ? " < " : "") << cardName(slot[j]);
                }
                std::cout << "\n";
            } else {
                // opponent: hand + parking hidden, only counts shown
                int parked = 0;
                for (auto& s : p.parking) parked += (int)s.size();
                std::cout << "\n      hand [" << p.hand.size() << "]: (hidden)"
                          << "   parking: " << parked << " card(s) (hidden)\n";
            }
        }
    }
};

// ---------- Move listing (shared by human + AI) ----------
struct Move { int pack; Game::Source src; int idx; };

static std::vector<Move> collectMoves(Game& g, Player& p) {
    std::vector<Move> moves;
    if (p.goalTop())
        for (int pk = 0; pk < (int)g.packs.size(); ++pk)
            if (Game::fits(*p.goalTop(), g.packs[pk])) moves.push_back({ pk, Game::GOAL, 0 });
    for (int h = 0; h < (int)p.hand.size(); ++h)
        for (int pk = 0; pk < (int)g.packs.size(); ++pk)
            if (Game::fits(p.hand[h], g.packs[pk])) moves.push_back({ pk, Game::HAND, h });
    for (int s = 0; s < Game::NUM_SLOTS; ++s)
        if (!p.parking[s].empty())
            for (int pk = 0; pk < (int)g.packs.size(); ++pk)
                if (Game::fits(p.parking[s].back(), g.packs[pk])) moves.push_back({ pk, Game::SLOT, s });
    return moves;
}

static bool canStartNew(const Card& c) { return c.isPuppet() || c.rank == 1; }

// Does playing this move complete its pack?
static bool moveCompletes(const Game& g, const Move& m) {
    return (int)g.packs[m.pack].cards.size() == 11;   // one away from A..Q
}

// ---------- AI ----------
// A turn is a SEQUENCE of plays. The AI keeps playing good moves, then parks
// one card to end the turn. Strategy per play: complete packs, burn the goal
// deck, avoid feeding the opponent's goal top. It parks (ends turn) when it has
// no good move left, or when it has played enough to feel the turn is "done."
static void aiTakeTurn(Game& g) {
    Player& p = g.players[g.turn];
    const Player& opp = g.players[1 - g.turn];
    int oppNeed = -1;
    if (opp.goalTop()) {
        int r = opp.goalTop()->rank;
        oppNeed = (r == 13) ? -1 : r;
    }

    int playsThisTurn = 0;
    while (g.winner < 0) {
        auto moves = collectMoves(g, p);

        // Park (end turn) if there's no move at all.
        if (moves.empty()) break;

        // Pick the best move this step.
        Move chosen = moves[0];
        bool haveChoice = false;

        // (a) completing a pack is always best
        for (auto& m : moves) if (moveCompletes(g, m)) { chosen = m; haveChoice = true; break; }

        // (b) otherwise burn the goal top if it fits somewhere
        if (!haveChoice && p.goalTop()) {
            for (auto& m : moves) if (m.src == Game::GOAL) { chosen = m; haveChoice = true; break; }
        }

        // (c) otherwise play a hand/slot card that does NOT feed the opponent
        if (!haveChoice) {
            auto playedRank = [&](const Move& m) {
                const Card* c = m.src == Game::SLOT ? &p.parking[m.idx].back() : &p.hand[m.idx];
                return c->rank;
            };
            std::vector<Move> rest;
            for (auto& m : moves) if (m.src != Game::GOAL) rest.push_back(m);
            std::sort(rest.begin(), rest.end(), [&](const Move& a, const Move& b) {
                bool ba = (oppNeed > 0 && playedRank(a) == oppNeed);
                bool bb = (oppNeed > 0 && playedRank(b) == oppNeed);
                if (ba != bb) return ba < bb;
                int ra = playedRank(a), rb = playedRank(b);
                int sa = (ra == 1 || ra == 13) ? 100 : ra;
                int sb = (rb == 1 || rb == 13) ? 100 : rb;
                if (sa != sb) return sa < sb;
                return a.src == Game::SLOT && b.src != Game::SLOT;
            });
            if (!rest.empty()) { chosen = rest[0]; haveChoice = true; }
        }

        if (!haveChoice) break;   // nothing sensible — park to end turn

        g.place(chosen.pack, chosen.src, chosen.idx);
        ++playsThisTurn;

        // Voluntary stop: after a few plays, if the remaining moves would only
        // feed the opponent, park to hold back (the "hold the 9" play).
        if (playsThisTurn >= 2) {
            auto after = collectMoves(g, p);
            bool allFeed = !after.empty();
            auto playedRank = [&](const Move& m) {
                const Card* c = m.src == Game::SLOT ? &p.parking[m.idx].back() : &p.hand[m.idx];
                return c->rank;
            };
            for (auto& m : after) if (!(oppNeed > 0 && playedRank(m) == oppNeed)) { allFeed = false; break; }
            if (allFeed) break;
        }
    }

    // End the turn by parking one card (forced or voluntary).
    if (g.winner < 0) {
        if (!p.hand.empty()) {
            int worst = 0;
            for (int i = 1; i < (int)p.hand.size(); ++i) {
                auto score = [](const Card& c) { return (c.rank == 1 || c.rank == 13) ? -1 : c.rank; };
                if (score(p.hand[i]) > score(p.hand[worst])) worst = i;
            }
            int slot = 0;
            for (int s = 1; s < Game::NUM_SLOTS; ++s)
                if (p.parking[s].size() < p.parking[slot].size()) slot = s;
            g.park(slot, worst);
        } else {
            g.advanceTurn();   // no hand card to park with — just pass
        }
    }
}

// ---------- Console loop ----------
static bool readLine(std::string& out, const std::string& prompt) {
    std::cout << prompt; std::cout.flush();
    return static_cast<bool>(std::getline(std::cin, out));
}
static bool readInt(int& out, const std::string& prompt, int lo, int hi) {
    std::string line;
    while (true) {
        if (!readLine(line, prompt)) return false;
        try { out = std::stoi(line); } catch (...) {
            std::cout << "  (enter " << lo << "-" << hi << ")\n"; continue;
        }
        if (out >= lo && out <= hi) return true;
        std::cout << "  (enter " << lo << "-" << hi << ")\n";
    }
}

// Aggressive bot for the self-test: plays a full turn (many plays), then parks.
// Eagerly starts packs with Puppets/Aces and always extends the longest pack.
static void aggressiveTurn(Game& g) {
    Player& p = g.players[g.turn];
    while (g.winner < 0) {
        auto moves = collectMoves(g, p);
        if (moves.empty()) break;

        Move chosen = moves[0];
        bool have = false;
        // 1) complete a pack
        for (auto& m : moves) if (moveCompletes(g, m)) { chosen = m; have = true; break; }
        // 2) extend the longest pack
        if (!have) {
            int best = 0;
            for (size_t i = 1; i < moves.size(); ++i)
                if (g.packs[moves[i].pack].cards.size() > g.packs[moves[best].pack].cards.size()) best = (int)i;
            chosen = moves[best]; have = true;
        }
        if (!have) break;
        g.place(chosen.pack, chosen.src, chosen.idx);
    }
    // start new packs with goal-top / hand / slot Ace or Puppet, as long as we can
    while (g.winner < 0) {
        auto startWith = [&](Game::Source src, int idx) {
            g.packs.push_back(Pack{});
            g.place((int)g.packs.size() - 1, src, idx);
        };
        if (p.goalTop() && canStartNew(*p.goalTop())) { startWith(Game::GOAL, 0); continue; }
        bool started = false;
        for (int h = 0; h < (int)p.hand.size(); ++h)
            if (canStartNew(p.hand[h])) { startWith(Game::HAND, h); started = true; break; }
        if (started) continue;
        for (int s = 0; s < Game::NUM_SLOTS; ++s)
            if (!p.parking[s].empty() && canStartNew(p.parking[s].back())) { startWith(Game::SLOT, s); started = true; break; }
        if (started) continue;
        break;   // nothing to start with
    }
    // End the turn by parking one card.
    if (g.winner < 0) {
        if (!p.hand.empty()) {
            int worst = 0;
            for (int i = 1; i < (int)p.hand.size(); ++i)
                if (p.hand[i].rank > p.hand[worst].rank) worst = i;
            int slot = 0;
            for (int s = 1; s < Game::NUM_SLOTS; ++s)
                if (p.parking[s].size() < p.parking[slot].size()) slot = s;
            g.park(slot, worst);
        } else {
            g.advanceTurn();
        }
    }
}

// ---------- Self-test: two AIs play a full game, asserting invariants ----------
static int runSelfTest() {
    std::cout << "=== SELF-TEST: CPU vs CPU (aggressive) ===\n";
    Game g;
    g.start("CPU-A", "CPU-B");

    auto invariant = [&]() {
        // card conservation: hand + goal + parking + draw + packs == 104
        int total = (int)g.draw.size();
        for (auto& pk : g.packs) total += (int)pk.cards.size();
        for (auto& pl : g.players) {
            total += (int)pl.hand.size() + (int)pl.goal.size();
            for (auto& s : pl.parking) total += (int)s.size();
        }
        if (total != 104) { std::cerr << "FAIL: card count = " << total << " != 104\n"; return false; }
        // packs never exceed A..Q and are in order
        for (auto& pk : g.packs)
            for (size_t i = 0; i < pk.cards.size(); ++i)
                if (pk.cards[i].rank != (int)i + 1) { std::cerr << "FAIL: pack order broken\n"; return false; }
        // hands never exceed 5
        for (auto& pl : g.players)
            if (!pl.done && (int)pl.hand.size() > Game::HAND_SIZE) { std::cerr << "FAIL: hand > 5\n"; return false; }
        return true;
    };

    int turns = 0;
    const int MAX_TURNS = 500000;
    bool diag = (std::getenv("T25_DIAG") != nullptr);
    while (g.winner < 0 && turns < MAX_TURNS) {
        aggressiveTurn(g);
        ++turns;
        if (turns % 1000 == 0 && !invariant()) return 1;
        if (diag && turns % 50000 == 0) {
            std::cout << "--- turn " << turns << " ---\n";
            std::cout << "  draw=" << g.draw.size() << " packs=" << g.packs.size();
            for (size_t i = 0; i < g.packs.size(); ++i)
                std::cout << " [" << (i+1) << "]" << g.packs[i].describe();
            std::cout << "\n";
            for (auto& pl : g.players) {
                std::cout << "  " << pl.name << " goal=" << pl.goal.size()
                          << " top=" << (pl.goalTop()?cardName(*pl.goalTop()):std::string("-"))
                          << " hand=" << pl.hand.size();
                for (auto& c : pl.hand) std::cout << " " << cardName(c);
                std::cout << " park=";
                for (auto& s : pl.parking) std::cout << "(" << s.size() << ")";
                std::cout << "\n";
            }
        }
    }
    if (!invariant()) return 1;
    if (g.winner < 0) { std::cerr << "FAIL: no winner after " << turns << " turns\n"; return 1; }

    std::cout << "  winner: " << g.players[g.winner].name
              << "  after " << turns << " turns\n";
    std::cout << "  PASS: game terminated, all invariants held (104 cards conserved,\n"
              << "         packs in A..Q order, hands <= 5).\n";
    return 0;
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--selftest") return runSelfTest();

    std::cout << "================ 25 CARDS ================\n";
    std::string n0, n1;
    readLine(n0, "Player 1 name (Enter = Player 1): ");
    if (n0.empty()) n0 = "Player 1";
    std::string modeLine;
    readLine(modeLine, "Player 2: human or AI? (Enter = AI): ");
    bool aiOpponent = !(modeLine == "human" || modeLine == "h" || modeLine == "2");
    readLine(n1, "Player 2 name (Enter = " + std::string(aiOpponent ? "CPU" : "Player 2") + "): ");
    if (n1.empty()) n1 = aiOpponent ? "CPU" : "Player 2";

    Game g;
    g.start(n0, n1);

    bool hotseat = !aiOpponent;   // two humans passing one terminal
    int lastShown = -1;
    while (g.winner < 0) {
        // Pass-and-play: when the turn changes between two humans, clear the
        // screen and ask the next player to take the terminal so they don't
        // see the previous player's hand.
        if (hotseat && g.turn != lastShown) {
            if (lastShown != -1) {
                std::cout << "\n\n";
                std::string line;
                readLine(line, "\n>>> Pass the terminal to " + g.players[g.turn].name +
                             " — press Enter when ready: ");
                // crude clear
                std::cout << "\033[2J\033[1;1";
            }
            lastShown = g.turn;
        }

        // Reveal only the active human's hand/parking; the AI's are hidden.
        g.printState(aiOpponent ? 0 : (int)g.turn);
        std::cout << "\n=== " << g.players[g.turn].name << "'s turn ===\n";
        Player& p = g.players[g.turn];

        // AI takes its full turn automatically (plays until it parks).
        if (aiOpponent && g.turn == 1) {
            aiTakeTurn(g);
            continue;
        }

        // --- Human turn: a LOOP of plays until the player parks ---
        bool turnOver = false;
        while (!turnOver && g.winner < 0) {
            auto moves = collectMoves(g, p);
            bool goalStarts = p.goalTop() && canStartNew(*p.goalTop());
            bool handStarts = false, slotStarts = false;
            for (auto& c : p.hand) handStarts |= canStartNew(c);
            for (int s = 0; s < Game::NUM_SLOTS; ++s)
                if (!p.parking[s].empty()) slotStarts |= canStartNew(p.parking[s].back());
            bool anyPlay = !moves.empty() || goalStarts || handStarts || slotStarts;

            if (!anyPlay) {
                // No moves at all — must park to end the turn.
                if (p.hand.empty()) {
                    std::cout << "  " << p.name << " has nothing to play or park — skipped.\n";
                    g.advanceTurn();
                    turnOver = true;
                    break;
                }
                std::cout << "  No playable cards. You MUST park one to end your turn.\n";
            }

            // List moves.
            int num = 0;
            auto label = [&](const Move& m) {
                std::string src = m.src == Game::GOAL ? "goal-top" : m.src == Game::SLOT ? "slot" + std::to_string(m.idx + 1) : "hand";
                const Card* c = m.src == Game::GOAL ? p.goalTop() : m.src == Game::SLOT ? &p.parking[m.idx].back() : &p.hand[m.idx];
                return std::to_string(++num) + ". " + src + " " + cardName(*c) + " -> pack " + std::to_string(m.pack + 1);
            };
            if (anyPlay) {
                std::cout << "  Playable:\n";
                for (auto& m : moves) std::cout << "    " << label(m) << "\n";
                if (goalStarts) std::cout << "    " << std::to_string(++num) << ". goal-top " << cardName(*p.goalTop()) << " -> NEW pack\n";
                if (handStarts)
                    for (int h = 0; h < (int)p.hand.size(); ++h)
                        if (canStartNew(p.hand[h])) std::cout << "    " << std::to_string(++num) << ". hand " << cardName(p.hand[h]) << " -> NEW pack\n";
                if (slotStarts)
                    for (int s = 0; s < Game::NUM_SLOTS; ++s)
                        if (!p.parking[s].empty() && canStartNew(p.parking[s].back()))
                            std::cout << "    " << std::to_string(++num) << ". slot" << (s + 1) << " " << cardName(p.parking[s].back()) << " -> NEW pack\n";
            }

            int total = (int)moves.size() + (goalStarts ? 1 : 0);
            if (handStarts) total += (int)std::count_if(p.hand.begin(), p.hand.end(), [](const Card& c){ return c.isPuppet() || c.rank == 1; });
            if (slotStarts) for (int s = 0; s < Game::NUM_SLOTS; ++s)
                if (!p.parking[s].empty() && (p.parking[s].back().isPuppet() || p.parking[s].back().rank == 1)) total++;

            int choice;
            if (!readInt(choice, "  Pick 1-" + std::to_string(total) + " (0 = PARK & end turn): ", 0, total)) return 1;

            if (choice == 0) {
                // Park ends the turn.
                if (p.hand.empty()) { std::cout << "  (nothing to park — must play)\n"; continue; }
                int which, slot;
                if (!readInt(which, "  Card to park (1-" + std::to_string(p.hand.size()) + "): ", 1, (int)p.hand.size())) return 1;
                if (!readInt(slot, "  Parking slot (1-4): ", 1, Game::NUM_SLOTS)) return 1;
                g.park(slot - 1, which - 1);
                turnOver = true;
                break;
            }

            // Resolve a play (turn continues).
            int k = choice - 1;
            if (k < (int)moves.size()) {
                const Move& m = moves[k];
                g.place(m.pack, m.src, m.idx);
            } else {
                k -= (int)moves.size();
                if (k == 0 && goalStarts) { g.packs.push_back(Pack{}); g.place((int)g.packs.size() - 1, Game::GOAL, 0); }
                else {
                    int hi = 0;
                    for (int h = 0; h < (int)p.hand.size(); ++h) {
                        if (!canStartNew(p.hand[h])) continue;
                        if (k == hi) { g.packs.push_back(Pack{}); g.place((int)g.packs.size() - 1, Game::HAND, h); break; }
                        ++hi;
                    }
                    if (k > hi) {
                        int si = 0; k -= hi + 1;
                        for (int s = 0; s < Game::NUM_SLOTS; ++s) {
                            if (p.parking[s].empty() || !canStartNew(p.parking[s].back())) continue;
                            if (k == si) { g.packs.push_back(Pack{}); g.place((int)g.packs.size() - 1, Game::SLOT, s); break; }
                            ++si;
                        }
                    }
                }
            }
            // brief pause so the player can read the result
            std::cout << "  ...\n";
        }
    }

    g.printState(aiOpponent ? 0 : (int)g.turn);
    std::cout << "\n*** " << g.players[g.winner].name << " WINS! ***\n";
    return 0;
}
