/*
The MIT License (MIT)

Copyright (c) 2015-2020 Howard Chan
https://github.com/howard-chan/HSM

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
//========== System Headers =====================
#include <iostream>
#include <stdint.h>
#include <vector>

//========== Local Headers ======================
#include "hsm.hpp"

//========== Namespace ==========================
using namespace std;
namespace hsm {

//========== Defines / Macros ===================
//========== Forward Declaration ================
//========== Static Variables ===================
// Singleton
static HsmState root;

//========== Function Definitions ===============
//-----------------------------------------------
// Class HsmState member function definitions
//-----------------------------------------------
// Default statehandler which is reserved for root
HsmState::HsmState() : pxParent{nullptr}, pcName{":ROOT:"}, pfnHandler{Hsm::root_handler}, ucLevel{0} {}

HsmState::HsmState(const char *pcName, handler_t pfnHandler, HsmState *pxParent)
    : pcName{pcName}, pfnHandler{pfnHandler}, pxParent{pxParent}
{
    if (!this->pxParent) {
        this->pxParent = &root;
    }
    ucLevel = this->pxParent->ucLevel + 1;
}

//-----------------------------------------------
// Class Hsm member function definitions
//-----------------------------------------------
hsm_event_t Hsm::root_handler(Hsm *This, hsm_event_t xEvent, void *pvParam)
{
#ifdef HSM_DEBUG_EVT2STR
    const char *pcEvtStr = This->pfnEvt2Str(xEvent);
    if (pcEvtStr)
    {
        HSM_DEBUG(HSM_COLOR_YEL "\tEvent:%s dropped, No Parent handling of %s[%s] param %#lx" HSM_COLOR_NON,
                  pcEvtStr, This->pcName, This->pxCurState->pcName, (unsigned long)pvParam);
    }
    else
#endif // HSM_DEBUG_EVT2STR
    {
        HSM_DEBUG(HSM_COLOR_YEL "\tEvent:%#lx dropped, No Parent handling of %s[%s] param %#lx" HSM_COLOR_NON,
                  (unsigned long)xEvent, This->pcName, This->pxCurState->pcName, (unsigned long)pvParam);
    }
    return HSME_NULL;
}

void Hsm::start(void)
{
    HSM_DEBUGC1("Starting %s[%s]()", pcName, pxCurState->pcName);
    // Invoke ENTRY and INIT event
    HSM_DEBUGC2("  %s[%s](ENTRY)", pcName, pxCurState->pcName);
    (*pxCurState)(this, HSME_ENTRY, 0);
#if HSM_FEATURE_INIT
    HSM_DEBUGC2("  %s[%s](INIT)", pcName, pxCurState->pcName);
    (*pxCurState)(this, HSME_INIT, 0);
#endif // HSM_FEATURE_INIT

    // // https://www.giannistsakiris.com/2012/09/07/c-calling-a-member-function-pointer/
    // (this->*((Hsm*)this)->Hsm::pxCurState->HsmState::pfnHandler) (HSME_ENTRY, 0);
    // (this->*((Hsm*)this)->Hsm::pxCurState->HsmState::pfnHandler) (HSME_INIT, 0);
 }

bool Hsm::isInState(HsmState *pxState) {
    HsmState *pxCur;
    // Traverse the parents to find the matching state.
    for (pxCur = pxCurState; pxCur; pxCur = pxCur->pxParent)
    {
        if (pxState == pxCur)
        {
            // Match found, HSM is in state or parent state
            return true;
        }
    }
    // This HSM is not in state or parent state
    return false;
};

void Hsm::operator()(hsm_event_t xEvent, void *pvParam)
{
    // This runs the state's event handler and forwards unhandled events to
    // the parent state
    HsmState *pxState = pxCurState;

#ifdef HSM_DEBUG_EVT2STR
    const char *pcEvtStr = pfnEvt2Str(xEvent);
    if (pcEvtStr)
    {
        HSM_DEBUGC1("Run %s[%s](evt:%s, param:%#lx)", pcName, pxState->pcName, pcEvtStr, (unsigned long)pvParam);
    }
    else
#endif // HSM_DEBUG_EVT2STR
    {
        HSM_DEBUGC1("Run %s[%s](evt:%#lx, param:%#lx)", pcName, pxState->pcName, (unsigned long)xEvent, (unsigned long)pvParam);
    }

    // Run until event has been consumed by state handler
    while (xEvent)
    {
        // xEvent = pxState->pfnHandler(pxHsm, xEvent, pvParam);
        xEvent = (*pxState)(this, xEvent, pvParam);
        pxState = pxState->pxParent;
        if (xEvent)
        {
#ifdef HSM_DEBUG_EVT2STR
            pcEvtStr = pfnEvt2Str(xEvent);
            if (pcEvtStr)
            {
                HSM_DEBUGC1("  evt:%s unhandled, passing to %s[%s]", pcEvtStr, pcName, pxState->pcName);
            }
            else
#endif // HSM_DEBUG_EVT2STR
            {
                HSM_DEBUGC1("  evt:%lx unhandled, passing to %s[%s]", (unsigned long)xEvent, pcName, pxState->pcName);
            }
        }
    }
#if HSM_FEATURE_DEBUG_ENABLE
    // Restore debug back to the configured debug
    bmDebug = bmDebugCfg;
#endif // HSM_FEATURE_DEBUG_ENABLE
}

void Hsm::tran(HsmState *pxNextState, void *pvParam, void (*method)(Hsm *This, void *pvParam))
{
#if HSM_FEATURE_SAFETY_CHECK
    // [optional] Check for illegal call to HSM_Tran in HSME_ENTRY or HSME_EXIT
    if (this->bIsTran)
    {
        HSM_DEBUG("!!!!Illegal call of HSM_Tran[%s -> %s] in HSME_ENTRY or HSME_EXIT Handler!!!!",
            this->pxCurState->pcName, pxNextState->pcName);
        return;
    }
    // Guard HSM_Tran() from certain recursive calls
    this->bIsTran = true;
#endif // HSM_FEATURE_SAFETY_CHECK

    vector<HsmState *> axList_exit;
    vector<HsmState *> axList_entry;
    // This performs the state transition with calls of exit, entry and init
    // Bulk of the work handles the exit and entry event during transitions
    HSM_DEBUGC2("Tran %s[%s -> %s]", this->pcName, this->pxCurState->pcName, pxNextState->pcName);
    // 1) Find the lowest common parent state
    HsmState *src = this->pxCurState;
    HsmState *dst = pxNextState;
    // 1a) Equalize the levels
    while (src->ucLevel != dst->ucLevel)
    {
        if (src->ucLevel > dst->ucLevel)
        {
            // source is deeper
            axList_exit.push_back(src);
            src = src->pxParent;
        }
        else
        {
            // destination is deeper
            axList_entry.push_back(dst);
            dst = dst->pxParent;
        }
    }
    // 1b) find the common parent
    while (src != dst)
    {
        axList_exit.push_back(src);
        src = src->pxParent;
        axList_entry.push_back(dst);
        dst = dst->pxParent;
    }
    // 2) Process all the exit events
    for (auto src : axList_exit)
    {
        HSM_DEBUGC3("  %s[%s](EXIT)", this->pcName, src->pcName);
        (*src)(this, HSME_EXIT, pvParam);
    }
    // 3) Call the transitional method hook
    if (method)
    {
        method(this, pvParam);
    }
    // 4) Process all the entry events
    for (auto dst = axList_entry.rbegin(); dst != axList_entry.rend(); ++dst)
    {
        HSM_DEBUGC3("  %s[%s](ENTRY)", this->pcName, (*dst)->pcName);
        (**dst)(this, HSME_ENTRY, pvParam);
    }
    // 5) Now we can set the destination state
    this->pxCurState = pxNextState;

#if HSM_FEATURE_SAFETY_CHECK
    this->bIsTran = false;
#endif // HSM_FEATURE_SAFETY_CHECK

#if HSM_FEATURE_INIT
    // 6) Invoke INIT signal, NOTE: Only HSME_INIT can recursively call HSM_Tran()
    HSM_DEBUGC3("  %s[%s](INIT)", this->pcName, pxNextState->pcName);
    (*this->pxCurState)(this, HSME_INIT, pvParam);
#endif // HSM_FEATURE_INIT
}
} // namespace hsm
