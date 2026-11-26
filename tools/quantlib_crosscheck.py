#!/usr/bin/env python3
"""Cross-checks `fit` against QuantLib (pip install QuantLib).

Run from the repository root after building the release preset:

    python3 tools/quantlib_crosscheck.py [path/to/fit]

Exits non-zero when any comparison is outside tolerance. This is a development aid for
validating numerical changes (see docs/PR_STANDARDS.md); it is not part of the build.

The cubic-zero scheme is not compared: QuantLib's zero-curve spline adds a node at t = 0,
so the two curves legitimately differ between pillars.

The monotone-convex scheme is compared with QuantLib's ConvexMonotone(0, 1, false), which is
the published Hagan-West method (ADR 0014), and only inside the pillar range: beyond the last
pillar QuantLib holds the last discrete forward, fit the last instantaneous forward.

Beyond the last pillar, flat-forward extrapolation (ADR 0015) is compared for the flat-forward
and linear-zero curves, whose QuantLib counterparts hold the instantaneous forward there.

The SOFR OIS curve is compared with OISRateHelper, and the 3M projection curve built on it with
SwapRateHelper and an exogenous discounting curve (ADR 0016). Futures and FRAs are compared with
SofrFutureRateHelper and FraRateHelper, and basis swaps with OvernightIborBasisSwapRateHelper
and IborIborBasisSwapRateHelper. Payment lags are compared with those helpers' paymentLag, and
lagged irs quotes are repriced with QuantLib's FixedRateLeg and IborLeg on fit's curves.
"""
import json
import subprocess
import sys

import QuantLib as ql

FIT = sys.argv[1] if len(sys.argv) > 1 else "build/release/fit"
DATA = "examples/data/"
failures = 0
def fit(*a):
    r=subprocess.run([FIT,*a,"--format","json"],capture_output=True,text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return json.loads(r.stdout)
def D(s): y,m,d=map(int,s.split('-')); return ql.Date(d,m,y)
def ql_bond(issue,mat,cpn,freq,dc,cal=ql.NullCalendar(),conv=ql.Unadjusted,eom=True):
    sch=ql.Schedule(D(issue),D(mat),ql.Period(freq),cal,ql.Unadjusted,ql.Unadjusted,ql.DateGeneration.Backward,eom)
    if dc=="icma": dcc=ql.ActualActual(ql.ActualActual.ISMA,sch)
    elif dc=="30/360": dcc=ql.Thirty360(ql.Thirty360.USA)
    elif dc=="act/365": dcc=ql.Actual365Fixed()
    elif dc=="30e/360": dcc=ql.Thirty360(ql.Thirty360.European)
    return ql.FixedRateBond(0,100.0,sch,[cpn/100],dcc,conv), dcc
def cmp(name,a,b,tol=1e-8):
    global failures
    ok=abs(a-b)<=tol
    failures += not ok
    print(f"{'OK ' if ok else 'BAD'} {name}: fit={a:.10f} ql={b:.10f} diff={a-b:.2e}")
cases=[("2016-11-15","2036-11-15",4.25,2,"icma","act/act","2026-11-18",4.40),
       ("2026-01-10","2031-11-15",5.5,2,"30/360","30/360","2026-03-03",6.1),
       ("2025-03-31","2030-08-31",3.0,2,"icma","act/act","2026-06-10",2.2),
       ("2024-02-29","2034-02-28",6.0,4,"30e/360","30e/360","2027-01-20",7.3),
       ("2026-01-10","2031-11-15",5.5,1,"icma","act/act","2026-03-03",6.1),
       ("2021-05-20","2046-05-20",2.75,2,"act/365","act/365","2026-09-01",4.9)]
for issue,mat,cpn,f,qdc,fdc,settle,y in cases:
    print("case",issue,mat,cpn,f,fdc,settle)
    b,dcc=ql_bond(issue,mat,cpn,{1:ql.Annual,2:ql.Semiannual,4:ql.Quarterly}[f],qdc)
    s=D(settle); ql.Settings.instance().evaluationDate=s
    comp={1:ql.Annual,2:ql.Semiannual,4:ql.Quarterly}[f]
    r=fit("bond","price","--issue",issue,"--maturity",mat,"--coupon",str(cpn),"--frequency",str(f),"--daycount",fdc,"--settle",settle,"--yield",str(y),"--calendar","none")["bond"]
    cmp("accrued",r["accrued"],b.accruedAmount(s))
    cmp("clean",r["clean_price"],ql.BondFunctions.cleanPrice(b,y/100,dcc,ql.Compounded,comp,s),1e-7)
    ir=ql.InterestRate(y/100,dcc,ql.Compounded,comp)
    cmp("moddur",r["modified_duration"],ql.BondFunctions.duration(b,ir,ql.Duration.Modified,s),1e-7)
    cmp("macdur",r["macaulay_duration"],ql.BondFunctions.duration(b,ir,ql.Duration.Macaulay,s),1e-7)
    cmp("convexity",r["convexity"],ql.BondFunctions.convexity(b,ir,s),1e-6)
    px=r["clean_price"]
    r2=fit("bond","yield","--issue",issue,"--maturity",mat,"--coupon",str(cpn),"--frequency",str(f),"--daycount",fdc,"--settle",settle,"--price",repr(px),"--calendar","none")["bond"]
    cmp("yield roundtrip",r2["yield_pct"],y,1e-8)

# curve
asof="2026-11-17"; a=D(asof); ql.Settings.instance().evaluationDate=a
cal=ql.WeekendsOnly()
quotes=[("deposit","1M",4.32),("deposit","3M",4.28),("deposit","6M",4.20),("par","1Y",4.05),("par","2Y",3.90),("par","3Y",3.88),("par","5Y",3.95),("par","7Y",4.08),("par","10Y",4.25),("par","20Y",4.62),("par","30Y",4.58)]
hs=[]
for t,ten,r in quotes:
    p=ql.Period(ten)
    if t=="deposit":
        hs.append(ql.DepositRateHelper(ql.QuoteHandle(ql.SimpleQuote(r/100)),p,0,cal,ql.ModifiedFollowing,False,ql.Actual360()))
    else:
        mat=a+p
        sch=ql.Schedule(a,mat,ql.Period(ql.Semiannual),cal,ql.Unadjusted,ql.Unadjusted,ql.DateGeneration.Backward,False)
        hs.append(ql.FixedRateBondHelper(ql.QuoteHandle(ql.SimpleQuote(100.0)),0,100.0,sch,[r/100],ql.ActualActual(ql.ActualActual.ISMA,sch),ql.Following,100.0))
curves=[("flat-forward",lambda: ql.PiecewiseLogLinearDiscount(a,hs,ql.Actual365Fixed())),
        ("linear-zero",lambda: ql.PiecewiseLinearZero(a,hs,ql.Actual365Fixed())),
        ("monotone-convex",lambda: ql.PiecewiseConvexMonotoneForward(
            a,hs,ql.Actual365Fixed(),[],[],ql.ConvexMonotone(0.0,1.0,False)))]
for interp,make in curves:
    c=make(); c.enableExtrapolation()
    nodes=fit("curve","build","--curve","examples/data/usd_treasury.csv","--asof",asof,"--interp",interp)["nodes"]
    print(interp)
    for n in nodes:
        cmp("df "+n["tenor"],n["discount"],c.discount(D(n["pillar"])),1e-9)
    q=fit("curve","query","--curve","examples/data/usd_treasury.csv","--asof",asof,"--interp",interp,"--dates","2027-08-03,2030-01-15,2041-06-30,2050-02-02")["curve"]
    for n in q: cmp("interp df "+n["date"],n["discount"],c.discount(D(n["date"])),1e-9)
    # bond off curve + zspread
    b,dcc=ql_bond("2016-11-15","2036-11-15",4.25,ql.Semiannual,"icma",cal,ql.Following)
    s=D("2026-11-18")
    h=ql.YieldTermStructureHandle(c)
    zs=ql.ZeroSpreadedTermStructure(h,ql.QuoteHandle(ql.SimpleQuote(0.0075)),ql.Continuous,ql.NoFrequency,ql.Actual365Fixed())
    npv=sum(cf.amount()*zs.discount(cf.date()) for cf in b.cashflows() if cf.date()>s)/zs.discount(s)
    r=fit("bond","price","--maturity","2036-11-15","--coupon","4.25","--settle","2026-11-18","--curve","examples/data/usd_treasury.csv","--asof",asof,"--interp",interp,"--zspread","75")["bond"]
    cmp("curve dirty z=75",r["dirty_price"],npv,1e-8)
# extrapolation beyond the last pillar (2056-11-17): QuantLib holds the instantaneous forward
print("extrapolation")
beyond="2057-03-31,2060-01-01,2066-11-17,2076-11-17"
for interp,cls in [("flat-forward",ql.PiecewiseLogLinearDiscount),("linear-zero",ql.PiecewiseLinearZero)]:
    c=cls(a,hs,ql.Actual365Fixed()); c.enableExtrapolation()
    q=fit("curve","query","--curve","examples/data/usd_treasury.csv","--asof",asof,"--interp",interp,"--extrapolation","flat-forward","--dates",beyond)["curve"]
    for n in q: cmp(interp+" extrap df "+n["date"],n["discount"],c.discount(D(n["date"])),1e-9)
# OIS swaps
print("sofr")
cal=ql.UnitedStates(ql.UnitedStates.GovernmentBond)
sq=[("deposit","1W",4.33),("deposit","1M",4.31),("deposit","3M",4.24),("deposit","6M",4.12),("swap","1Y",3.96),("swap","2Y",3.74),("swap","3Y",3.68),("swap","5Y",3.71),("swap","7Y",3.80),("swap","10Y",3.93),("swap","15Y",4.08),("swap","20Y",4.12),("swap","30Y",4.02)]
nodes=fit("curve","build","--curve","examples/data/usd_sofr.csv","--asof",asof,"--spot-lag","2","--calendar","us")["nodes"]
spot=cal.advance(a,2,ql.Days)
for (t,ten,r),n in zip(sq,nodes):
    end=cal.adjust(spot+ql.Period(ten),ql.ModifiedFollowing)
    ok = str(end.ISO()) == n["pillar"]
    failures += not ok
    print(("OK " if ok else "BAD"), "pillar", ten, n["pillar"], end.ISO())
# OISRateHelper defaults: annual fixed leg on the index day count (ACT/360), periods accruing
# between modified-following adjusted dates, no payment lag. An overnight index on the US
# government bond calendar matches fit's `us` calendar.
on=ql.OvernightIndex("SOFR-GB",0,ql.USDCurrency(),cal,ql.Actual360())
ois=[]
for t,ten,r in sq:
    if t=="deposit":
        ois.append(ql.DepositRateHelper(ql.QuoteHandle(ql.SimpleQuote(r/100)),ql.Period(ten),2,cal,ql.ModifiedFollowing,False,ql.Actual360()))
    else:
        ois.append(ql.OISRateHelper(2,ql.Period(ten),ql.QuoteHandle(ql.SimpleQuote(r/100)),on))
sofr=ql.PiecewiseLogLinearDiscount(a,ois,ql.Actual365Fixed())
for n in nodes: cmp("sofr df "+n["tenor"],n["discount"],sofr.discount(D(n["pillar"])),1e-9)
q=fit("curve","query","--curve","examples/data/usd_sofr.csv","--asof",asof,"--spot-lag","2","--calendar","us","--dates","2027-08-03,2030-01-15,2041-06-30,2050-02-02")["curve"]
for n in q: cmp("sofr interp df "+n["date"],n["discount"],sofr.discount(D(n["date"])),1e-9)
# Dual curve (ADR 0016): the 3M projection curve on the SOFR OIS curve. SwapRateHelper with an
# exogenous discounting curve; withIndexedCoupons=False gives par coupons, as fit uses.
print("dual curve")
ibor=ql.IborIndex("TSOFR3M",ql.Period("3M"),2,ql.USDCurrency(),cal,ql.ModifiedFollowing,False,ql.Actual360())
dh=ql.YieldTermStructureHandle(sofr)
pq=[("deposit","3M",4.29),("irs","1Y",4.02),("irs","2Y",3.81),("irs","3Y",3.76),("irs","5Y",3.79),("irs","7Y",3.88),("irs","10Y",4.01),("irs","15Y",4.16),("irs","20Y",4.20),("irs","30Y",4.10)]
ph=[]
for t,ten,r in pq:
    rq=ql.QuoteHandle(ql.SimpleQuote(r/100))
    if t=="deposit":
        ph.append(ql.DepositRateHelper(rq,ql.Period(ten),2,cal,ql.ModifiedFollowing,False,ql.Actual360()))
    else:
        ph.append(ql.SwapRateHelper(rq,ql.Period(ten),cal,ql.Annual,ql.ModifiedFollowing,ql.Actual360(),ibor,
                                    ql.QuoteHandle(),ql.Period(0,ql.Days),dh,ql.nullInt(),ql.Pillar.LastRelevantDate,ql.Date(),False,False))
proj=ql.PiecewiseLogLinearDiscount(a,ph,ql.Actual365Fixed())
dual=["--discount-curve","examples/data/usd_sofr.csv","--projection-curve","examples/data/usd_term_sofr_3m.csv","--asof",asof,"--spot-lag","2","--calendar","us"]
for n in fit("curve","build",*dual)["nodes"]:
    if n["curve"]=="projection": cmp("projection df "+n["tenor"],n["discount"],proj.discount(D(n["pillar"])),1e-9)
q=fit("curve","query",*dual,"--dates","2027-08-03,2030-01-15,2041-06-30,2050-02-02")["curve"]
for n in q: cmp("projection interp df "+n["date"],n["discount"],proj.discount(D(n["date"])),1e-9)
# Short-end instruments: a SOFR curve with a 3M SOFR futures strip (SofrFutureRateHelper, with
# convexity adjustments) and a 3M projection curve with FRAs (FraRateHelper) on the OIS curve.
print("futures and FRAs")
imm={"H":ql.March,"M":ql.June,"U":ql.September,"Z":ql.December}
fq=[("deposit","1W",4.33,0),("deposit","1M",4.31,0),("future","Z6",95.79,0.1),("future","H7",96.02,0.3),
    ("future","M7",96.20,0.6),("future","U7",96.30,1.0),("future","Z7",96.33,1.4),("swap","2Y",3.74,0),
    ("swap","3Y",3.68,0),("swap","5Y",3.71,0),("swap","7Y",3.80,0),("swap","10Y",3.93,0),("swap","15Y",4.08,0),
    ("swap","20Y",4.12,0),("swap","30Y",4.02,0)]
fh=[]
for t,ten,r,ca in fq:
    if t=="deposit":
        fh.append(ql.DepositRateHelper(ql.QuoteHandle(ql.SimpleQuote(r/100)),ql.Period(ten),2,cal,ql.ModifiedFollowing,False,ql.Actual360()))
    elif t=="future":
        fh.append(ql.SofrFutureRateHelper(r,imm[ten[0]],2020+int(ten[1]),ql.Quarterly,ca/1e4))
    else:
        fh.append(ql.OISRateHelper(2,ql.Period(ten),ql.QuoteHandle(ql.SimpleQuote(r/100)),on))
fut=ql.PiecewiseLogLinearDiscount(a,fh,ql.Actual365Fixed())
for n in fit("curve","build","--curve","examples/data/usd_sofr_futures.csv","--asof",asof,"--spot-lag","2","--calendar","us")["nodes"]:
    cmp("futures df "+n["tenor"],n["discount"],fut.discount(D(n["pillar"])),1e-9)
rq=[("deposit","3M",4.29),("fra","3x6",3.97),("fra","6x9",3.78),("fra","9x12",3.70),("irs","2Y",3.81),("irs","3Y",3.76),
    ("irs","5Y",3.79),("irs","7Y",3.88),("irs","10Y",4.01),("irs","15Y",4.16),("irs","20Y",4.20),("irs","30Y",4.10)]
rh=[]
for t,ten,r in rq:
    rqh=ql.QuoteHandle(ql.SimpleQuote(r/100))
    if t=="deposit":
        rh.append(ql.DepositRateHelper(rqh,ql.Period(ten),2,cal,ql.ModifiedFollowing,False,ql.Actual360()))
    elif t=="fra":
        s_,e_=map(int,ten.split("x"))
        rh.append(ql.FraRateHelper(rqh,s_,e_,2,cal,ql.ModifiedFollowing,False,ql.Actual360()))
    else:
        rh.append(ql.SwapRateHelper(rqh,ql.Period(ten),cal,ql.Annual,ql.ModifiedFollowing,ql.Actual360(),ibor,
                                    ql.QuoteHandle(),ql.Period(0,ql.Days),dh,ql.nullInt(),ql.Pillar.LastRelevantDate,ql.Date(),False,False))
fra=ql.PiecewiseLogLinearDiscount(a,rh,ql.Actual365Fixed())
fradual=["--discount-curve","examples/data/usd_sofr.csv","--projection-curve","examples/data/usd_term_sofr_3m_fra.csv","--asof",asof,"--spot-lag","2","--calendar","us"]
for n in fit("curve","build",*fradual)["nodes"]:
    if n["curve"]=="projection": cmp("fra curve df "+n["tenor"],n["discount"],fra.discount(D(n["pillar"])),1e-9)
# Basis swaps (ADR 0019): the 3M curve from SOFR / 3M Term SOFR basis swaps on the SOFR curve
# (OvernightIborBasisSwapRateHelper; the reference leg is the discount curve's index), and the
# 6M curve from 3M / 6M basis swaps against the 3M curve (IborIborBasisSwapRateHelper). In both
# the spread is paid on the reference (base) leg and each leg pays at its index's frequency.
print("basis swaps")
dates="2027-08-03,2030-01-15,2041-06-30,2050-02-02"
for interp,cls in [("flat-forward",ql.PiecewiseLogLinearDiscount),("linear-zero",ql.PiecewiseLinearZero)]:
    base=cls(a,ois,ql.Actual365Fixed())
    bh=ql.YieldTermStructureHandle(base)
    on_f=ql.OvernightIndex("SOFR-GB",0,ql.USDCurrency(),cal,ql.Actual360(),bh)
    # 3M from SOFR basis swaps.
    bq=[("deposit","3M",4.29),("basis","1Y",5.9),("basis","2Y",6.9),("basis","3Y",7.9),("basis","5Y",7.9),("basis","7Y",7.9),
        ("basis","10Y",7.9),("basis","15Y",7.9),("basis","20Y",7.9),("basis","30Y",7.9)]
    i3=ql.IborIndex("TSOFR3M",ql.Period("3M"),2,ql.USDCurrency(),cal,ql.ModifiedFollowing,False,ql.Actual360())
    hs=[]
    for t,ten,r in bq:
        if t=="deposit":
            hs.append(ql.DepositRateHelper(ql.QuoteHandle(ql.SimpleQuote(r/100)),ql.Period(ten),2,cal,ql.ModifiedFollowing,False,ql.Actual360()))
        else:
            hs.append(ql.OvernightIborBasisSwapRateHelper(ql.QuoteHandle(ql.SimpleQuote(r/1e4)),ql.Period(ten),2,cal,ql.ModifiedFollowing,False,on_f,i3,bh))
    c3=cls(a,hs,ql.Actual365Fixed())
    b3=["--discount-curve","examples/data/usd_sofr.csv","--projection-curve","examples/data/usd_term_sofr_3m_basis.csv","--asof",asof,"--spot-lag","2","--calendar","us","--interp",interp]
    for n in fit("curve","build",*b3)["nodes"]:
        if n["curve"]=="projection": cmp(interp+" sofr basis df "+n["tenor"],n["discount"],c3.discount(D(n["pillar"])),1e-9)
    for n in fit("curve","query",*b3,"--dates",dates)["curve"]:
        cmp(interp+" sofr basis interp df "+n["date"],n["discount"],c3.discount(D(n["date"])),1e-9)
    # 6M from 3M / 6M basis swaps against the 3M curve of the example set.
    tq=[("deposit","3M",4.29),("irs","1Y",4.02),("irs","2Y",3.81),("irs","3Y",3.76),("irs","5Y",3.79),("irs","7Y",3.88),
        ("irs","10Y",4.01),("irs","15Y",4.16),("irs","20Y",4.20),("irs","30Y",4.10)]
    th=[]
    for t,ten,r in tq:
        rqh=ql.QuoteHandle(ql.SimpleQuote(r/100))
        if t=="deposit":
            th.append(ql.DepositRateHelper(rqh,ql.Period(ten),2,cal,ql.ModifiedFollowing,False,ql.Actual360()))
        else:
            th.append(ql.SwapRateHelper(rqh,ql.Period(ten),cal,ql.Annual,ql.ModifiedFollowing,ql.Actual360(),i3,
                                        ql.QuoteHandle(),ql.Period(0,ql.Days),bh,ql.nullInt(),ql.Pillar.LastRelevantDate,ql.Date(),False,False))
    t3=cls(a,th,ql.Actual365Fixed())
    i3f=ql.IborIndex("TSOFR3M",ql.Period("3M"),2,ql.USDCurrency(),cal,ql.ModifiedFollowing,False,ql.Actual360(),ql.YieldTermStructureHandle(t3))
    i6=ql.IborIndex("TSOFR6M",ql.Period("6M"),2,ql.USDCurrency(),cal,ql.ModifiedFollowing,False,ql.Actual360())
    sq6=[("deposit","6M",4.14),("basis","1Y",4.6),("basis","2Y",4.9),("basis","3Y",5.1),("basis","5Y",5.3),("basis","7Y",5.4),
         ("basis","10Y",5.5),("basis","15Y",5.5),("basis","20Y",5.4),("basis","30Y",5.2)]
    h6=[]
    for t,ten,r in sq6:
        if t=="deposit":
            h6.append(ql.DepositRateHelper(ql.QuoteHandle(ql.SimpleQuote(r/100)),ql.Period(ten),2,cal,ql.ModifiedFollowing,False,ql.Actual360()))
        else:
            h6.append(ql.IborIborBasisSwapRateHelper(ql.QuoteHandle(ql.SimpleQuote(r/1e4)),ql.Period(ten),2,cal,ql.ModifiedFollowing,False,i3f,i6,bh,False))
    c6=cls(a,h6,ql.Actual365Fixed())
    s6=["--curve-set","examples/data/usd_curves.csv","--asof",asof,"--interp",interp]
    for n in fit("curve","build",*s6)["nodes"]:
        if n["curve"]=="term-sofr-6m": cmp(interp+" 3s6s basis df "+n["tenor"],n["discount"],c6.discount(D(n["pillar"])),1e-9)
    for n in fit("curve","query",*s6,"--curve-name","term-sofr-6m","--dates",dates)["curve"]:
        cmp(interp+" 3s6s basis interp df "+n["date"],n["discount"],c6.discount(D(n["date"])),1e-9)
# Payment lags: every leg of a swap, irs or basis quote pays 2 business days after each period's
# adjusted end, and the pillar is the last payment date. SOFR OIS and the basis curves are
# bootstrapped with OISRateHelper and the basis helpers' paymentLag. QuantLib has no lagged
# SwapRateHelper, so lagged irs quotes are repriced instead: fit's built curves are rebuilt in
# QuantLib from their nodes (log-linear discount factors, which is flat-forward) and each swap is
# priced with FixedRateLeg and IborLeg with paymentLag.
print("payment lags")
lag=2
lh=[]
for t,ten,r in sq:
    if t=="deposit":
        lh.append(ql.DepositRateHelper(ql.QuoteHandle(ql.SimpleQuote(r/100)),ql.Period(ten),2,cal,ql.ModifiedFollowing,False,ql.Actual360()))
    else:
        lh.append(ql.OISRateHelper(2,ql.Period(ten),ql.QuoteHandle(ql.SimpleQuote(r/100)),on,ql.YieldTermStructureHandle(),False,lag))
lsofr=ql.PiecewiseLogLinearDiscount(a,lh,ql.Actual365Fixed())
lagged=["--asof",asof,"--spot-lag","2","--calendar","us","--payment-lag",str(lag)]
lnodes=fit("curve","build","--curve","examples/data/usd_sofr.csv",*lagged)["nodes"]
for n in lnodes: cmp("lagged sofr df "+n["tenor"],n["discount"],lsofr.discount(D(n["pillar"])),1e-9)
for n in fit("curve","query","--curve","examples/data/usd_sofr.csv",*lagged,"--dates",dates)["curve"]:
    cmp("lagged sofr interp df "+n["date"],n["discount"],lsofr.discount(D(n["date"])),1e-9)
lbh=ql.YieldTermStructureHandle(lsofr)
lon=ql.OvernightIndex("SOFR-GB",0,ql.USDCurrency(),cal,ql.Actual360(),lbh)
lb=[]
for t,ten,r in bq:
    if t=="deposit":
        lb.append(ql.DepositRateHelper(ql.QuoteHandle(ql.SimpleQuote(r/100)),ql.Period(ten),2,cal,ql.ModifiedFollowing,False,ql.Actual360()))
    else:
        lb.append(ql.OvernightIborBasisSwapRateHelper(ql.QuoteHandle(ql.SimpleQuote(r/1e4)),ql.Period(ten),2,cal,ql.ModifiedFollowing,False,lon,i3,lbh,False,lag))
lc3=ql.PiecewiseLogLinearDiscount(a,lb,ql.Actual365Fixed())
lb3=["--discount-curve","examples/data/usd_sofr.csv","--projection-curve","examples/data/usd_term_sofr_3m_basis.csv",*lagged]
for n in fit("curve","build",*lb3)["nodes"]:
    if n["curve"]=="projection": cmp("lagged sofr basis df "+n["tenor"],n["discount"],lc3.discount(D(n["pillar"])),1e-9)
# 6M from 3M / 6M basis swaps against that 3M basis curve, all lagged, through a curve set.
import os, tempfile
setfile=os.path.join(tempfile.mkdtemp(),"lagged_set.csv")
with open(setfile,"w") as f:
    root=os.path.abspath(DATA)
    f.write(f"name,quotes,discount,reference,calendar,spot_lag,payment_lag\n"
            f"sofr,{root}/usd_sofr.csv,,,us,2,{lag}\n"
            f"basis-3m,{root}/usd_term_sofr_3m_basis.csv,sofr,,us,2,{lag}\n"
            f"basis-6m,{root}/usd_term_sofr_6m.csv,sofr,basis-3m,us,2,{lag}\n")
li3=ql.IborIndex("TSOFR3M",ql.Period("3M"),2,ql.USDCurrency(),cal,ql.ModifiedFollowing,False,ql.Actual360(),ql.YieldTermStructureHandle(lc3))
l6=[]
for t,ten,r in sq6:
    if t=="deposit":
        l6.append(ql.DepositRateHelper(ql.QuoteHandle(ql.SimpleQuote(r/100)),ql.Period(ten),2,cal,ql.ModifiedFollowing,False,ql.Actual360()))
    else:
        l6.append(ql.IborIborBasisSwapRateHelper(ql.QuoteHandle(ql.SimpleQuote(r/1e4)),ql.Period(ten),2,cal,ql.ModifiedFollowing,False,li3,i6,lbh,False,None,ql.DateGeneration.Backward,lag))
lc6=ql.PiecewiseLogLinearDiscount(a,l6,ql.Actual365Fixed())
for n in fit("curve","build","--curve-set",setfile,"--asof",asof)["nodes"]:
    if n["curve"]=="basis-6m": cmp("lagged 3s6s basis df "+n["tenor"],n["discount"],lc6.discount(D(n["pillar"])),1e-9)
os.remove(setfile)
# Lagged irs: reprice each quote with QuantLib legs on fit's curves.
def from_nodes(nodes):
    return ql.DiscountCurve([a]+[D(n["pillar"]) for n in nodes],[1.0]+[n["discount"] for n in nodes],ql.Actual365Fixed())
li=["--discount-curve","examples/data/usd_sofr.csv","--projection-curve","examples/data/usd_term_sofr_3m.csv",*lagged]
built=fit("curve","build",*li)["nodes"]
disc=from_nodes([n for n in built if n["curve"]=="discount"])
proj_nodes=[n for n in built if n["curve"]=="projection"]
projq=from_nodes(proj_nodes)
pidx=ql.IborIndex("TSOFR3M",ql.Period("3M"),2,ql.USDCurrency(),cal,ql.ModifiedFollowing,False,ql.Actual360(),ql.YieldTermStructureHandle(projq))
spot=cal.advance(a,2,ql.Days)
for n in proj_nodes:
    if n["type"]!="irs": continue
    end=spot+ql.Period(n["tenor"])
    fs=ql.Schedule(spot,end,ql.Period("1Y"),cal,ql.ModifiedFollowing,ql.ModifiedFollowing,ql.DateGeneration.Backward,False)
    fl=ql.Schedule(spot,end,ql.Period("3M"),cal,ql.ModifiedFollowing,ql.ModifiedFollowing,ql.DateGeneration.Backward,False)
    fixed=ql.FixedRateLeg(fs,ql.Actual360(),[1.0],[0.01],paymentAdjustment=ql.Following,paymentCalendar=cal,paymentLag=lag)
    flt=ql.IborLeg([1.0],fl,pidx,ql.Actual360(),ql.Following,paymentCalendar=cal,paymentLag=lag,withIndexedCoupons=False)
    sw=ql.Swap(fixed,flt); sw.setPricingEngine(ql.DiscountingSwapEngine(ql.YieldTermStructureHandle(disc)))
    ok_pillar = str(max(c.date() for c in list(fixed)+list(flt)).ISO()) == n["pillar"]
    failures += not ok_pillar
    print(("OK " if ok_pillar else "BAD"), "lagged irs pillar", n["tenor"], n["pillar"])
    cmp("lagged irs fair rate "+n["tenor"],n["quote_pct"]/100,abs(sw.legNPV(1)/sw.legNPV(0))*0.01,1e-12)
print(f"{failures} failure(s)")
sys.exit(1 if failures else 0)
