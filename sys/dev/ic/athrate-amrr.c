/*	$NetBSD: athrate-amrr.c,v 1.13 2019/11/10 21:16:35 chs Exp $ */

/*-
 * Copyright (c) 2004 INRIA
 * Copyright (c) 2002-2005 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 */

#include <sys/cdefs.h>
#ifdef __FreeBSD__
__FBSDID("$FreeBSD: src/sys/dev/ath/ath_rate/amrr/amrr.c,v 1.10 2005/08/09 10:19:43 rwatson Exp $");
#endif
#ifdef __NetBSD__
__KERNEL_RCSID(0, "$NetBSD: athrate-amrr.c,v 1.13 2019/11/10 21:16:35 chs Exp $");
#endif

/*
 * AMRR rate control. See:
 * http://www-sop.inria.fr/rapports/sophia/RR-5208.html
 * "IEEE 802.11 Rate Adaptation: A Practical Approach" by
 *    Mathieu Lacage, Hossein Manshaei, Thierry Turletti
 */
#include "opt_inet.h"

#include <sys/param.h>
#include <sys/systm.h> 
#include <sys/sysctl.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/device.h>
#include <sys/bus.h>
#include <sys/socket.h>
 
#include <net/if.h>
#include <net/if_media.h>
#include <net/if_arp.h>
#include <net/if_ether.h>		/* XXX for ether_sprintf */

#include <net80211/ieee80211_var.h>

#include <net/bpf.h>

#ifdef INET
#include <netinet/in.h> 
#endif

#include "ah_desc.h"
#include <dev/ic/athvar.h>
#include <dev/ic/athrate-amrr.h>

#include <external/isc/atheros_hal/dist/ah.h>

#define	AMRR_DEBUG
#ifdef AMRR_DEBUG
#define	DPRINTF(sc, _fmt, ...) do {					\
	if (sc->sc_debug & 0x10)					\
		printf(_fmt, __VA_ARGS__);				\
} while (0)
#else
#define	DPRINTF(sc, _fmt, ...)
#endif

static	int ath_rateinterval = 1000;		/* rate ctl interval (ms)  */
static	int ath_rate_max_success_threshold = 10;
static	int ath_rate_min_success_threshold = 1;

static void	ath_ratectl(void *);
static void	ath_rate_update(struct ath_softc *, struct ieee80211_node *,
			int rate);
static void	ath_rate_ctl_start(struct ath_softc *, struct ieee80211_node *);
static void	ath_rate_ctl(void *, struct ieee80211_node *);

void
ath_rate_node_init(struct ath_softc *sc, struct ath_node *an)
{
	/* NB: assumed to be zero'd by caller */
	ath_rate_update(sc, &an->an_node, 0);
}

void
ath_rate_node_cleanup(struct ath_softc *sc, struct ath_node *an)
{
}

void
ath_rate_findrate(struct ath_softc *sc, struct ath_node *an,
	int shortPreamble, size_t frameLen,
	u_int8_t *rix, int *try0, u_int8_t *txrate)
{
	struct amrr_node *amn = ATH_NODE_AMRR(an);

	*rix = amn->amn_tx_rix0;
	*try0 = amn->amn_tx_try0;
	if (shortPreamble)
		*txrate = amn->amn_tx_rate0sp;
	else
		*txrate = amn->amn_tx_rate0;
}

void
ath_rate_setupxtxdesc(struct ath_softc *sc, struct ath_node *an,
	struct ath_desc *ds, int shortPreamble, u_int8_t rix)
{
	struct amrr_node *amn = ATH_NODE_AMRR(an);

	ath_hal_setupxtxdesc(sc->sc_ah, ds
		, amn->amn_tx_rate1sp, amn->amn_tx_try1	/* series 1 */
		, amn->amn_tx_rate2sp, amn->amn_tx_try2	/* series 2 */
		, amn->amn_tx_rate3sp, amn->amn_tx_try3	/* series 3 */
	);
}

void
ath_rate_tx_complete(struct ath_softc *sc, struct ath_node *an,
	const struct ath_desc *ds, const struct ath_desc *ds0)
{
	struct amrr_node *amn = ATH_NODE_AMRR(an);
	int sr = ds->ds_txstat.ts_shortretry;
	int lr = ds->ds_txstat.ts_longretry;
	int retry_count = sr + lr;

	amn->amn_tx_try0_cnt++;
	if (retry_count == 1) {
		amn->amn_tx_try1_cnt++;
	} else if (retry_count == 2) {
		amn->amn_tx_try1_cnt++;
		amn->amn_tx_try2_cnt++;
	} else if (retry_count == 3) {
		amn->amn_tx_try1_cnt++;
		amn->amn_tx_try2_cnt++;
		amn->amn_tx_try3_cnt++;
	} else if (retry_count > 3) {
		amn->amn_tx_try1_cnt++;
		amn->amn_tx_try2_cnt++;
		amn->amn_tx_try3_cnt++;
		amn->amn_tx_failure_cnt++;
	}
}

void
ath_rate_newassoc(struct ath_softc *sc, struct ath_node *an, int isnew)
{
	if (isnew)
		ath_rate_ctl_start(sc, &an->an_node);
}

static void 
node_reset (struct amrr_node *amn)
{
	amn->amn_tx_try0_cnt = 0;
	amn->amn_tx_try1_cnt = 0;
	amn->amn_tx_try2_cnt = 0;
	amn->amn_tx_try3_cnt = 0;
	amn->amn_tx_failure_cnt = 0;
  	amn->amn_success = 0;
  	amn->amn_recovery = 0;
  	amn->amn_success_threshold = ath_rate_min_success_threshold;
}


/**
 * The code below assumes that we are dealing with hardware multi rate retry
 * I have no idea what will happen if you try to use this module with another
 * type of hardware. Your machine might catch fire or it might work with
 * horrible performance...
 */
static void
ath_rate_update(struct ath_softc *sc, struct ieee80211_node *ni, int rate)
{
	struct ath_node *an = ATH_NODE(ni);
	struct amrr_node *amn = ATH_NODE_AMRR(an);
	const HAL_RATE_TABLE *rt = sc->sc_currates;
	u_int8_t rix;

	KASSERTMSG(rt != NULL, "no rate table, mode %u", sc->sc_curmode);

	DPRINTF(sc, "%s: set xmit rate for %s to %dM\n",
	    __func__, ether_sprintf(ni->ni_macaddr),
	    ni->ni_rates.rs_nrates > 0 ?
		(ni->ni_rates.rs_rates[rate] & IEEE80211_RATE_VAL) / 2 : 0);

	amn->amn_rix = rate;
	/*
	 * Before associating a node has no rate set setup
	 * so we can't calculate any transmit codes to use.
	 * This is ok since we should never be sending anything
	 * but management frames and those always go at the
	 * lowest hardware rate.
	 */
	if (ni->ni_rates.rs_nrates > 0) {
		ni->ni_txrate = ni->ni_rates.rs_rates[rate] & IEEE80211_RATE_VAL;
		amn->amn_tx_rix0 = sc->sc_rixmap[ni->ni_txrate];
		amn->amn_tx_rate0 = rt->info[amn->amn_tx_rix0].rateCode;
		amn->amn_tx_rate0sp = amn->amn_tx_rate0 |
			rt->info[amn->amn_tx_rix0].shortPreamble;
		if (sc->sc_mrretry) {
			amn->amn_tx_try0 = 1;
			amn->amn_tx_try1 = 1;
			amn->amn_tx_try2 = 1;
			amn->amn_tx_try3 = 1;
			if (--rate >= 0) {
				rix = sc->sc_rixmap[
						    ni->ni_rates.rs_rates[rate]&IEEE80211_RATE_VAL];
				amn->amn_tx_rate1 = rt->info[rix].rateCode;
				amn->amn_tx_rate1sp = amn->amn_tx_rate1 |
					rt->info[rix].shortPreamble;
			} else {
				amn->amn_tx_rate1 = amn->amn_tx_rate1sp = 0;
			}
			if (--rate >= 0) {
				rix = sc->sc_rixmap[
						    ni->ni_rates.rs_rates[rate]&IEEE80211_RATE_VAL];
				amn->amn_tx_rate2 = rt->info[rix].rateCode;
				amn->amn_tx_rate2sp = amn->amn_tx_rate2 |
					rt->info[rix].shortPreamble;
			} else {
				amn->amn_tx_rate2 = amn->amn_tx_rate2sp = 0;
			}
			if (rate > 0) {
				/* NB: only do this if we didn't already do it above */
				amn->amn_tx_rate3 = rt->info[0].rateCode;
				amn->amn_tx_rate3sp =
					amn->amn_tx_rate3 | rt->info[0].shortPreamble;
			} else {
				amn->amn_tx_rate3 = amn->amn_tx_rate3sp = 0;
			}
		} else {
			amn->amn_tx_try0 = ATH_TXMAXTRY;
			/* theorically, these statements are useless because
			 *  the code which uses them tests for an_tx_try0 == ATH_TXMAXTRY
			 */
			amn->amn_tx_try1 = 0;
			amn->amn_tx_try2 = 0;
			amn->amn_tx_try3 = 0;
			amn->amn_tx_rate1 = amn->amn_tx_rate1sp = 0;
			amn->amn_tx_rate2 = amn->amn_tx_rate2sp = 0;
			amn->amn_tx_rate3 = amn->amn_tx_rate3sp = 0;
		}
	}
	node_reset (amn);
}

/*
 * Set the starting transmit rate for a node.
 */
static void
ath_rate_ctl_start(struct ath_softc *sc, struct ieee80211_node *ni)
{
#define	RATE(_ix)	(ni->ni_rates.rs_rates[(_ix)] & IEEE80211_RATE_VAL)
	int srate;

	KASSERTMSG(ni->ni_rates.rs_nrates > 0, "no rates");
#if 0
	if (ic->ic_fixed_rate == IEEE80211_FIXED_RATE_NONE) {
#endif
		/*
		 * No fixed rate is requested. For 11b start with
		 * the highest negotiated rate; otherwise, for 11g
		 * and 11a, we start "in the middle" at 24Mb or 36Mb.
		 */
		srate = ni->ni_rates.rs_nrates - 1;
		if (sc->sc_curmode != IEEE80211_MODE_11B) {
			/*
			 * Scan the negotiated rate set to find the
			 * closest rate.
			 */
			/* NB: the rate set is assumed sorted */
			for (; srate >= 0 && RATE(srate) > 72; srate--)
				;
			KASSERTMSG(srate >= 0, "bogus rate set");
		}
#if 0
	} else {
		/*
		 * A fixed rate is to be used; ic_fixed_rate is an
		 * index into the supported rate set.  Convert this
		 * to the index into the negotiated rate set for
		 * the node.  We know the rate is there because the
		 * rate set is checked when the station associates.
		 */
		const struct ieee80211_rateset *rs =
			&ic->ic_sup_rates[ic->ic_curmode];
		int r = rs->rs_rates[ic->ic_fixed_rate] & IEEE80211_RATE_VAL;
		/* NB: the rate set is assumed sorted */
		srate = ni->ni_rates.rs_nrates - 1;
		for (; srate >= 0 && RATE(srate) != r; srate--)
			;
		KASSERTMSG(srate >= 0,
			"fixed rate %d not in rate set", ic->ic_fixed_rate);
	}
#endif
	ath_rate_update(sc, ni, srate);
#undef RATE
}

static void
ath_rate_cb(void *arg, struct ieee80211_node *ni)
{
	struct ath_softc *sc = arg;

	ath_rate_update(sc, ni, 0);
}

/*
 * Reset the rate control state for each 802.11 state transition.
 */
void
ath_rate_newstate(struct ieee80211vap *vap, enum ieee80211_state state)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ath_softc *sc = ic->ic_softc;
	struct amrr_softc *asc = (struct amrr_softc *) sc->sc_rc;
	struct ieee80211_node *ni;

	if (state == IEEE80211_S_INIT) {
		callout_stop(&asc->timer);
		return;
	}
	if (ic->ic_opmode == IEEE80211_M_STA) {
		/*
		 * Reset local xmit state; this is really only
		 * meaningful when operating in station mode.
		 */
		ni = vap->iv_bss;
		if (state == IEEE80211_S_RUN) {
			ath_rate_ctl_start(sc, ni);
		} else {
			ath_rate_update(sc, ni, 0);
		}
	} else {
		/*
		 * When operating as a station the node table holds
		 * the AP's that were discovered during scanning.
		 * For any other operating mode we want to reset the
		 * tx rate state of each node.
		 */
		ieee80211_iterate_nodes(&ic->ic_sta, ath_rate_cb, sc);
		ath_rate_update(sc, vap->iv_bss, 0);
	}
#if 0
	if (ic->ic_fixed_rate == IEEE80211_FIXED_RATE_NONE &&
	    state == IEEE80211_S_RUN) {
#else
	if (state == IEEE80211_S_RUN) {
#endif
		int interval;
		/*
		 * Start the background rate control thread if we
		 * are not configured to use a fixed xmit rate.
		 */
		interval = ath_rateinterval;
		if (ic->ic_opmode == IEEE80211_M_STA)
			interval /= 2;
		callout_reset(&asc->timer, (interval * hz) / 1000,
			ath_ratectl, sc);
	}
}

/* 
 * Examine and potentially adjust the transmit rate.
 */
static void
ath_rate_ctl(void *arg, struct ieee80211_node *ni)
{
	struct ath_softc *sc = arg;
	struct amrr_node *amn = ATH_NODE_AMRR(ATH_NODE (ni));
	int rix;

#define is_success(amn) \
(amn->amn_tx_try1_cnt  < (amn->amn_tx_try0_cnt/10))
#define is_enough(amn) \
(amn->amn_tx_try0_cnt > 10)
#define is_failure(amn) \
(amn->amn_tx_try1_cnt > (amn->amn_tx_try0_cnt/3))

	rix = amn->amn_rix;
  
  	DPRINTF (sc, "cnt0: %d cnt1: %d cnt2: %d cnt3: %d -- threshold: %d\n",
		 amn->amn_tx_try0_cnt,
		 amn->amn_tx_try1_cnt,
		 amn->amn_tx_try2_cnt,
		 amn->amn_tx_try3_cnt,
		 amn->amn_success_threshold);
  	if (is_success (amn) && is_enough (amn)) {
		amn->amn_success++;
		if (amn->amn_success == amn->amn_success_threshold &&
		    rix + 1 < ni->ni_rates.rs_nrates) {
			amn->amn_recovery = 1;
			amn->amn_success = 0;
			rix++;
			DPRINTF (sc, "increase rate to %d\n", rix_;
  		} else {
			amn->amn_recovery = 0;
		}
  	} else if (is_failure (amn)) {
  		amn->amn_success = 0;
		if (rix > 0) {
  			if (amn->amn_recovery) {
  				/* recovery failure. */
  				amn->amn_success_threshold *= 2;
  				amn->amn_success_threshold =
					MIN(amn->amn_success_threshold,
					 (u_int)ath_rate_max_success_threshold);
 				DPRINTF (sc, "decrease rate recovery thr: %d\n", amn->amn_success_threshold);
  			} else {
  				/* simple failure. */
 				amn->amn_success_threshold = ath_rate_min_success_threshold;
 				DPRINTF (sc, "decrease rate normal thr: %d\n", amn->amn_success_threshold);
  			}
			amn->amn_recovery = 0;
			rix--;
   		} else {
			amn->amn_recovery = 0;
		}

   	}
	if (is_enough (amn) || rix != amn->amn_rix) {
		/* reset counters. */
		amn->amn_tx_try0_cnt = 0;
		amn->amn_tx_try1_cnt = 0;
		amn->amn_tx_try2_cnt = 0;
		amn->amn_tx_try3_cnt = 0;
		amn->amn_tx_failure_cnt = 0;
	}
	if (rix != amn->amn_rix) {
		ath_rate_update(sc, ni, rix);
	}
}

static void
ath_ratectl(void *arg)
{
	struct ath_softc *sc = arg;
	struct amrr_softc *asc = (struct amrr_softc *) sc->sc_rc;
	struct ieee80211com *ic = &sc->sc_ic;
	int interval;

	if (ic->ic_nrunning > 0) {
		sc->sc_stats.ast_rate_calls++;

		if (ic->ic_opmode == IEEE80211_M_STA) {
#if 0
			ath_rate_ctl(sc, ic->ic_bss);	/* NB: no reference */
#else
			struct ieee80211vap *vap;

			TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next)
				ath_rate_ctl(sc, vap->iv_bss);
#endif
		} else
			ieee80211_iterate_nodes(&ic->ic_sta, ath_rate_ctl, sc);
	}
	interval = ath_rateinterval;
	if (ic->ic_opmode == IEEE80211_M_STA)
		interval /= 2;
	callout_reset(&asc->timer, (interval * hz) / 1000,
		ath_ratectl, sc);
}

static void
ath_rate_sysctlattach(struct ath_softc *sc)
{
	struct sysctllog **clog = &sc->sc_sysctllog;
	const struct sysctlnode *cnode, *rnode;

	if ((rnode = ath_sysctl_treetop(NULL)) == NULL)
		return;

	SYSCTL_GLOBAL_INT(CTLFLAG_READWRITE, "rate_interval",
	    "rate control: operation interval (ms)", rateinterval);
	/* XXX bounds check values */
	SYSCTL_GLOBAL_INT(CTLFLAG_READWRITE, "max_success_threshold",
	    "rate control: max success threshold", rate_max_success_threshold);
	SYSCTL_GLOBAL_INT(CTLFLAG_READWRITE, "min_success_threshold",
	    "rate control: min success threshold", rate_min_success_threshold);
}

struct ath_ratectrl *
ath_rate_attach(struct ath_softc *sc)
{
	struct amrr_softc *asc;

	asc = malloc(sizeof(struct amrr_softc), M_DEVBUF, M_WAITOK|M_ZERO);
	asc->arc.arc_space = sizeof(struct amrr_node);
	callout_init(&asc->timer, 0);
	ath_rate_sysctlattach(sc);

	return &asc->arc;
}

void
ath_rate_detach(struct ath_ratectrl *arc)
{
	struct amrr_softc *asc = (struct amrr_softc *) arc;

	callout_drain(&asc->timer);
	free(asc, M_DEVBUF);
}
