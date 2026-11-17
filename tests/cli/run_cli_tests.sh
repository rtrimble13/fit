#!/usr/bin/env bash
# End-to-end tests of the fit command line. Usage: run_cli_tests.sh <path-to-fit>
# Run from the repository root (ctest does this automatically).
set -u

FIT="$1"
DATA="examples/data"
CURVE=(--curve "$DATA/usd_treasury.csv" --asof 2026-11-17)
BOND=(--maturity 2036-11-15 --coupon 4.25)
failures=0
OUT="$(mktemp)"
ERR="$(mktemp)"
trap 'rm -f "$OUT" "$ERR"' EXIT

fail() {
    echo "FAIL: $1"
    failures=$((failures + 1))
}

# expect_exit <code> <args...>: run fit, capture stdout/stderr, check the exit code.
expect_exit() {
    local want="$1"
    shift
    "$FIT" "$@" >"$OUT" 2>"$ERR"
    local got=$?
    [ "$got" -eq "$want" ] || fail "exit $got (wanted $want): fit $*"
}

# expect_out <regex>: stdout of the last run matches.
expect_out() { grep -Eq -- "$1" "$OUT" || fail "stdout lacks /$1/: $(head -c 300 "$OUT")"; }
expect_err() { grep -Eq -- "$1" "$ERR" || fail "stderr lacks /$1/: $(head -c 300 "$ERR")"; }
expect_no_out() { [ ! -s "$OUT" ] || fail "stdout should be empty: $(head -c 200 "$OUT")"; }

# ---- top level ----------------------------------------------------------------------------
expect_exit 0 --version;                 expect_out '^fit [0-9]+\.[0-9]+\.[0-9]+$'
expect_exit 0 --help;                    expect_out 'portfolio scenario'
expect_exit 2;                           expect_out 'Usage: fit'
expect_exit 0 bond --help;               expect_out 'bond forecast'
expect_exit 0 bond price --help;         expect_out '--maturity DATE'
expect_exit 2 frobnicate;                expect_err "fit: error: unknown command 'frobnicate'"; expect_no_out
expect_exit 2 bond explode;              expect_err "unknown command 'bond explode'"

# ---- usage errors exit 2, data errors exit 1, and neither writes to stdout ----------------
expect_exit 2 bond price --coupon 4;     expect_err "missing required option '--maturity'"; expect_no_out
expect_exit 2 bond price "${BOND[@]}" --settle 2026-11-18 --yield 4 --bogus 1
expect_err "unknown option '--bogus'"
expect_exit 2 bond price "${BOND[@]}" --settle 2026-11-18 --yield 4 --price 99
expect_err 'at most one of --price, --yield and --zspread'
expect_exit 2 bond price "${BOND[@]}" --settle 2026-11-18
expect_err 'give --price or --yield, or --curve'
expect_exit 2 bond price "${BOND[@]}" --yield 4
expect_err 'give --settle, or --asof'
expect_exit 2 bond price "${BOND[@]}" --settle 2026-11-18 --yield 4 --format xml
expect_exit 1 bond price "${BOND[@]}" --settle 2026-13-45 --yield 4
expect_err "fit: error: invalid date '2026-13-45'"; expect_no_out
expect_exit 1 bond price "${BOND[@]}" --settle 2026-11-18 --yield abc
expect_err "invalid number 'abc' for --yield"
expect_exit 1 bond price "${BOND[@]}" --settle 2040-01-01 --yield 4
expect_err 'has no cashflows after 2040-01-01'
expect_exit 1 curve build --curve /no/such.csv --asof 2026-11-17
expect_err "cannot open '/no/such.csv'"
BAD="$(mktemp)"
printf 'type,tenor,rate\npar,2Y,3.9\nfuture,5Y,4.0\n' >"$BAD"
expect_exit 1 curve build --curve "$BAD" --asof 2026-11-17
expect_err ":3: unknown quote type 'future'"
printf 'type,tenor,rate\npar,2Y,3.9\npar,24M,4.0\n' >"$BAD"
expect_exit 1 curve build --curve "$BAD" --asof 2026-11-17
expect_err 'mature on the same date'
rm -f "$BAD"

# ---- curve --------------------------------------------------------------------------------
expect_exit 0 curve build "${CURVE[@]}"
expect_out '^10Y +par +2036-11-17 +10\.0082 +4\.2500 +4\.2417 +0\.654084 +0\.000000$'
expect_out '^max_repricing_error_bp +0\.00000000$'
for interp in linear-zero flat-forward cubic-zero monotone-cubic monotone-convex; do
    expect_exit 0 curve build "${CURVE[@]}" --interp "$interp"
    expect_out "^interpolation +$interp$"
    expect_out '^max_repricing_error_bp +0\.00000000$'
done
expect_exit 1 curve build "${CURVE[@]}" --interp wobbly
expect_err "unknown interpolation .wobbly. .*monotone-convex"; expect_no_out
# Extrapolation: each scheme's natural choice by default, either one on request.
expect_exit 0 curve build "${CURVE[@]}";                      expect_out '^extrapolation +flat-forward$'
expect_exit 0 curve build "${CURVE[@]}" --interp linear-zero; expect_out '^extrapolation +flat-zero$'
expect_exit 0 curve build "${CURVE[@]}" --extrapolation flat-zero --format json
expect_out '"extrapolation": "flat-zero"'
expect_exit 0 curve query "${CURVE[@]}" --extrapolation flat-zero --tenors 30Y,50Y --format csv
expect_out '^30Y,2056-11-17,30\.0[0-9]*,4\.58346231'   # the 30Y pillar's zero rate...
expect_out '^50Y,2076-11-17,50\.0[0-9]*,4\.58346231'   # ...held flat to 50Y
expect_exit 1 curve build "${CURVE[@]}" --extrapolation sideways
expect_err "unknown extrapolation 'sideways' \(expected flat-forward or flat-zero\)"; expect_no_out
expect_exit 0 curve build --help;                             expect_out '--extrapolation METHOD'
expect_exit 0 curve query "${CURVE[@]}" --tenors 2Y,10Y
expect_out '^2Y +2028-11-17 .* 3\.9000 '   # par rate of a pillar is its quote
expect_exit 0 curve query "${CURVE[@]}" --tenors 5Y --horizon 1Y
expect_out '^forward_curve$'; expect_out '^5Y +2032-11-17'
expect_exit 0 curve forwards "${CURVE[@]}" --tenor 6M --end 2Y
expect_out '^2026-11-17 +2027-05-17 +0\.0000 +4\.2000 '  # first forward is the 6M deposit
expect_exit 0 curve build --curve "$DATA/usd_sofr.csv" --asof 2026-11-17 --spot-lag 2 --calendar us
expect_out '^1W +deposit +2026-11-27 '     # T+2 start, rolled over Thanksgiving

# ---- multi-curve: a projection curve on an OIS discount curve -----------------------------
SOFR=(--asof 2026-11-17 --spot-lag 2 --calendar us)
DUAL=(--discount-curve "$DATA/usd_sofr.csv" --projection-curve "$DATA/usd_term_sofr_3m.csv" "${SOFR[@]}")
expect_exit 0 curve build "${DUAL[@]}"
expect_out '^discount +discount +flat-forward +flat-forward +13 +1 +0\.00000000$'
expect_out '^projection +discount +flat-forward +flat-forward +10 '
expect_out '^projection +10Y +irs +2036-11-19 .* 4\.0100 .* 0\.000000$'
expect_out '^discount +2Y +swap +2028-11-20 '
expect_exit 0 curve build "${DUAL[@]}" --format json
expect_out '"curves": \['; expect_out '"discount_curve": "discount"'
expect_exit 0 curve forwards "${DUAL[@]}" --tenor 3M --end 1Y
expect_out '^2026-11-17 +2027-02-17 +0\.0000 +4\.2900 '   # projection curve: its 3M fixing (92 days)
# Bonds discount on the discount curve: the same price as on the OIS curve alone.
expect_exit 0 bond price "${BOND[@]}" "${DUAL[@]}" --format csv
DUAL_PRICE="$(grep '^clean_price,' "$OUT")"
expect_exit 0 bond price "${BOND[@]}" --curve "$DATA/usd_sofr.csv" "${SOFR[@]}" --format csv
[ "$DUAL_PRICE" = "$(grep '^clean_price,' "$OUT")" ] || fail "bond on dual curves: $DUAL_PRICE"
expect_exit 2 curve build "${CURVE[@]}" --discount-curve "$DATA/usd_sofr.csv"
expect_err 'give --curve, or --discount-curve with --projection-curve, not both'; expect_no_out
expect_exit 2 curve build --discount-curve "$DATA/usd_sofr.csv" "${SOFR[@]}"
expect_err '--discount-curve requires --projection-curve'
expect_exit 2 curve build --projection-curve "$DATA/usd_term_sofr_3m.csv" "${SOFR[@]}"
expect_err '--projection-curve requires --discount-curve'
expect_exit 2 curve build --asof 2026-11-17
expect_err "missing required option '--curve' \(or --discount-curve with --projection-curve\)"
expect_exit 1 curve build --discount-curve "$DATA/usd_sofr.csv" --projection-curve "$DATA/usd_treasury.csv" "${SOFR[@]}"
expect_err "^fit: error: projection curve: quote '1Y': a par bond quote prices on the curve itself"

# ---- bond ---------------------------------------------------------------------------------
# Published Excel PRICE() example.
expect_exit 0 bond price --maturity 2017-11-15 --coupon 5.75 --daycount 30/360 \
    --settle 2008-02-15 --yield 6.5
expect_out '^clean_price +94\.634362$'
expect_exit 0 bond yield --maturity 2016-11-15 --coupon 5.75 --daycount 30/360 \
    --settle 2008-02-15 --price 95.04287
expect_out '^yield_pct +6\.50000$'
expect_exit 2 bond yield "${BOND[@]}" --settle 2026-11-18
expect_exit 0 bond price "${BOND[@]}" "${CURVE[@]}" --price 99.9375 --face 10000000
expect_out '^settle +2026-11-18$'          # T+1 from the as-of date
expect_out '^settlement_amount +9,997,272\.10$'
expect_out '^zspread_bp +0\.572$'
expect_exit 0 bond price "${BOND[@]}" "${CURVE[@]}" --zspread 50 --format json
expect_out '"zspread_bp": 50,?$'
expect_exit 0 bond price "${BOND[@]}" --settle 2026-11-18 --yield 4.4 --format csv
expect_out '^clean_price,98\.7974063813$'
expect_exit 0 bond cashflows "${BOND[@]}" "${CURVE[@]}"
expect_out '^2036-11-17 +2036-05-15 +2036-11-15 +2\.125000 +100\.0000 +102\.125000 '
expect_exit 0 bond risk "${BOND[@]}" "${CURVE[@]}" --price 99.9375 --key-tenors 2Y,5Y,10Y,30Y
expect_out '^total +0\.0823[0-9]+ +100\.00$'  # key rates sum to the parallel DV01
expect_exit 1 bond risk "${BOND[@]}" "${CURVE[@]}" --key-tenors 10Y,5Y
expect_exit 0 bond forecast "${BOND[@]}" "${CURVE[@]}" --price 99.9375 --horizon 1Y \
    --shifts=-50,0,50,2Y:-25/10Y:25
expect_out '^horizon +2027-11-18$'
expect_out '^0bp +100\.2641 +4\.2144 +4\.2498 +0\.3266 +0\.0000 +0\.0420 +4\.6185 +4\.620 +4\.620$'
expect_out '^2Y:-25/10Y:25 '
expect_exit 0 bond forecast "${BOND[@]}" "${CURVE[@]}" --horizon 2027-06-30 --scenario forward
expect_exit 1 bond forecast "${BOND[@]}" "${CURVE[@]}" --horizon 20Y

# ---- portfolio and pretrade ---------------------------------------------------------------
PORT=(--portfolio "$DATA/portfolio.csv")
expect_exit 0 portfolio value "${PORT[@]}" "${CURVE[@]}"
expect_out '^market_value +73,329,137\.00$'
expect_out '^dv01 +43,811\.68$'
expect_out '^UST-7Y-SHORT +-5,000,000 '
expect_exit 0 portfolio risk "${PORT[@]}" "${CURVE[@]}" --key-tenors 2Y,5Y,10Y,30Y
expect_out '^TOTAL +6,087 +10,190 +18,535 +8,999 +43,812$'
expect_exit 0 portfolio scenario "${PORT[@]}" "${CURVE[@]}" --shifts=-50,50
expect_out '^-50bp +75,586,580\.50 +2,257,443\.50 +3\.079$'
expect_exit 0 portfolio cashflows "${PORT[@]}" "${CURVE[@]}" --bucket year
expect_out '^2028 +3,106,000\.00 +25,000,000\.00 '
expect_out '^TOTAL .* 73,000,000\.00 '
expect_exit 1 portfolio cashflows "${PORT[@]}" "${CURVE[@]}" --bucket decade
# --trades adds proposed trades to the book: totals equal the pretrade "after" column.
expect_exit 0 portfolio value "${PORT[@]}" --trades "$DATA/trades.csv" "${CURVE[@]}"
expect_out '^positions +10$'; expect_out '^dv01 +50,474\.24$'
expect_exit 0 pretrade --trades "$DATA/trades.csv" "${PORT[@]}" "${CURVE[@]}" --dv01-limit 60000
expect_out '^UST-2Y +SELL +10,000,000 '
expect_out '^dv01 +43,811\.68 +6,662\.56 +50,474\.24 +6,662\.56$'
expect_out '^status +OK$'
expect_exit 0 pretrade --trades "$DATA/trades.csv" "${PORT[@]}" "${CURVE[@]}" --dv01-limit 45000
expect_out '^status +BREACH$'; expect_err 'fit: warning: DV01 limit breached'
expect_exit 0 pretrade --trades "$DATA/trades.csv" "${CURVE[@]}"   # trades alone
expect_out '^market_value +0\.00 '

if [ "$failures" -ne 0 ]; then
    echo "$failures CLI test(s) failed"
    exit 1
fi
echo "all CLI tests passed"
