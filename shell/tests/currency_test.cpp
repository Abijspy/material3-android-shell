// Currency conversions in Spotlight: phrasings, symbols and names, a lone
// amount to the local currency, and what isn't one (fixed rates)
#include "services/Currency.h"

#include <cstdio>

using namespace m3;

static int failures = 0;

static void expect(const std::string& q, const std::string& text, const currency::Rates& r) {
    auto c = currency::convert(q, r, "INR");
    const std::string got = c ? c->text : "(none)";
    if (got != text) {
        std::fprintf(stderr, "FAIL: \"%s\" → %s, wanted %s\n", q.c_str(), got.c_str(), text.c_str());
        ++failures;
    }
}

int main() {
    currency::Rates r;
    r.date = "2026-11-02";
    r.perEuro = {{"USD", 1.10}, {"INR", 92.40}, {"GBP", 0.85}, {"JPY", 165.0}};
    expect("100 usd to inr", "8,400.00 INR", r);
    expect("100 USD in EUR", "90.91 EUR", r);
    expect("$100 in eur", "90.91 EUR", r);
    expect("50 euros to rupees", "4,620.00 INR", r);
    expect("100 usd", "8,400.00 INR", r);           // to the local currency
    expect("1,000 gbp to jpy", "194,118 JPY", r);   // thousands typed; the yen has no decimals
    expect("10 eur usd", "11.00 USD", r);
    expect("100 inr", "(none)", r);                 // already local
    expect("100 km", "(none)", r);
    expect("100 abc to usd", "(none)", r);
    expect("hello", "(none)", r);
    if (currency::forZone("Asia/Kolkata") != "INR" || currency::forZone("Europe/Berlin") != "EUR" ||
        currency::forZone("Europe/London") != "GBP" || currency::forZone("Antarctica/Troll") != "USD") {
        std::fprintf(stderr, "FAIL: currencies for zones\n");
        ++failures;
    }
    if (failures) std::fprintf(stderr, "%d failed\n", failures);
    return failures ? 1 : 0;
}
