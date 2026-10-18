#include "Audio/SSXSpeech.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace SSXSpeech
{
namespace
{
	uint16 U16(TConstArrayView<uint8> B, int64 P) { return P + 1 < B.Num() ? uint16(B[P] | (B[P + 1] << 8)) : 0; }
	uint32 U32(TConstArrayView<uint8> B, int64 P) { return P + 3 < B.Num() ? uint32(B[P] | (B[P + 1] << 8) | (B[P + 2] << 16) | (uint32(B[P + 3]) << 24)) : 0; }
	uint8 U8(TConstArrayView<uint8> B, int64 P) { return P < B.Num() ? B[P] : 0; }
	int64 Align4(int64 N) { return (N + 3) & ~int64(3); }
	constexpr int64 WeightMult[8] = { 1, 4, 16, 64, 256, 1024, 4096, 16384 };

	FSegment ParseSegment(TConstArrayView<uint8> B, int64 P)
	{
		FSegment S;
		S.GroupId = U16(B, P);
		S.ParamIndex = U8(B, P + 2);
		S.Kind = U8(B, P + 3);
		S.NumCond = int8(U8(B, P + 4));
		for (int32 J = 0; J < 4; ++J)
		{
			S.CondParam[J] = U8(B, P + 8 + J);
		}
		for (int32 J = 0; J < FMath::Clamp<int32>(S.NumCond, 0, 4); ++J)
		{
			S.CondMask[J] = U32(B, P + 16 + 4 * J);
		}
		return S;
	}

	FVariant ParseVariant(TConstArrayView<uint8> B, int64 P)
	{
		FVariant V;
		V.WeightByte = U8(B, P);
		V.CtxMask = U8(B, P + 1);
		V.CtxValue = U8(B, P + 2);
		const uint8 B3 = U8(B, P + 3);
		V.ModeRule = B3 & 3;
		V.NumSegments = B3 >> 2;
		for (int32 S = 0; S < V.NumSegments; ++S)
		{
			V.Segments.Add(ParseSegment(B, P + int64(U8(B, P + 4 + S)) * 4));
		}
		return V;
	}
}

int32 ChannelOfDb(int32 Db)
{
	return (Db == DbChar || Db == DbAnim) ? 1 : 0;
}

FRequest MakeRequest(std::initializer_list<uint32> Words)
{
	FRequest R;
	for (const uint32 W : Words)
	{
		if (R.Num() < RequestWords) R.Add(W);
	}
	while (R.Num() < RequestWords) R.Add(0);
	return R;
}

int64 FVariant::Weight() const { return int64(WeightByte & 31) * WeightMult[WeightByte >> 5]; }

bool FVariant::ModeOk(int32 Mode) const
{
	switch (ModeRule)
	{
	case 0: return true;
	case 1: return Mode == 2;
	case 2: return Mode != 2;
	default: return false;
	}
}

bool FEventDatabase::Parse(TConstArrayView<uint8> B)
{
	if (B.Num() < 0x18 || U16(B, 0) != 0x0803)
	{
		return false;
	}
	Db = B[8];
	Sub = B[9];
	const int32 N = U16(B, 0x10);
	ShortenLimit = U16(B, 0x14);
	InterruptPriority = U16(B, 0x16);
	Events.Reset();
	for (int32 I = 0; I < N; ++I)
	{
		const int64 Pos = int64(U16(B, 0x18 + 2 * I)) * 4;
		FEvent E;
		E.Id = U16(B, Pos);
		E.Timeout = U16(B, Pos + 2);
		E.Priority = U16(B, Pos + 4);
		const int32 NV = U8(B, Pos + 6);
		const int32 NCB = FMath::Max<int32>(int8(U8(B, Pos + 7)), 0);
		E.MaxRepeats = int8(U8(B, Pos + 8));
		E.Chance = int8(U8(B, Pos + 9));
		E.Flags = U8(B, Pos + 10);
		TArray<uint16> VOffs;
		for (int32 K = 0; K < NV; ++K) VOffs.Add(U16(B, Pos + 12 + 2 * K));
		int64 P = Pos + 12 + Align4(NV * 2);
		for (int32 K = 0; K < NCB; ++K)
		{
			E.Callbacks.Add({ U8(B, P + 3 * K), U8(B, P + 3 * K + 1), U8(B, P + 3 * K + 2) });
		}
		P += Align4(NCB * 3);
		if (E.Flags & FlagParamMask)
		{
			E.ParamMaskSelector = U32(B, P);
			for (int32 K = 0; K < NV; ++K) E.VariantParamMasks.Add(U16(B, P + 4 + 2 * K));
			P += Align4(NV * 2 + 4);
		}
		if (E.Flags & FlagFollowList)
		{
			E.bFollowList = true;
			const int32 NF = U8(B, P);
			for (int32 K = 0; K < NF; ++K) E.FollowList.Add(U16(B, P + 2 + 2 * K));
		}
		for (const uint16 V : VOffs)
		{
			E.Variants.Add(ParseVariant(B, Pos + int64(V) * 4));
		}
		Events.Add(MoveTemp(E));
	}
	return true;
}

const FEvent* FEventDatabase::Find(uint16 Id) const
{
	for (const FEvent& E : Events)
	{
		if (E.Id == Id) return &E;
	}
	return nullptr;
}

bool FSampleGroup::Parse(TConstArrayView<uint8> B, const FString& InPath)
{
	if (B.Num() < 12)
	{
		return false;
	}
	Path = InPath;
	TArray<FString> Parts;
	InPath.ParseIntoArray(Parts, TEXT("/"));
	Section = Parts.Num() >= 3 ? Parts[2].ToLower() : FString();
	DatStem = FPaths::GetBaseFilename(InPath);
	Id = U16(B, 0);
	Sub = U16(B, 2);
	Flags = B[4];
	Count = B[5];
	HistLen = B[6];
	Shift = B[7];
	TotalUnits = U16(B, 8);
	Pages = U16(B, 10);
	NumTags = Flags & 15;
	const int32 Es = NumTags + 2;
	Entries.Reset();
	for (int32 I = 0; I < Count; ++I)
	{
		TArray<uint8> E;
		for (int32 K = 0; K < Es; ++K) E.Add(U8(B, 12 + I * Es + K));
		Entries.Add(MoveTemp(E));
	}
	int64 P = 12 + int64(Count) * Es;
	const int32 NK = (Flags >> 4) & 7;
	Keys.Reset();
	for (int32 K = 0; K < NK; ++K)
	{
		const uint8 Hi = U8(B, P + 2 * K), Lo = U8(B, P + 2 * K + 1);
		if (!Keys.Contains(Hi >> 5)) Keys.Add(Hi >> 5, ((Hi << 8) | Lo) & 0x1FFF);
	}
	P += 2 * NK;
	bHasEnabled = (Flags & 0x80) != 0;
	Enabled.Reset();
	if (bHasEnabled)
	{
		const int32 NB = (Count + 7) / 8 + 1;
		for (int32 K = 1; K < NB; ++K) Enabled.Add(U8(B, P + K));   // bit i lives in byte 1 + i / 8 of the region
		P += NB;
	}
	History.Reset();
	HistoryCursor = 0;
	if (HistLen)
	{
		HistoryCursor = U8(B, P);
		for (int32 K = 0; K < HistLen; ++K) History.Add(U8(B, P + 1 + K));
		P += HistLen + 1;
	}
	PageBits.Reset();
	for (int64 K = P; K < B.Num(); ++K) PageBits.Add(B[K]);
	return true;
}

void FSampleGroup::Span(int32 I, int32 Page, int64& OutOffset, int32& OutLength) const
{
	int32 Idx = I;
	const TArray<uint8>* E = &Entries[I];
	if ((*E)[0] & 0x80)
	{
		Idx = (*E)[1];
		E = &Entries[Idx];
	}
	const int64 Start = int64(((*E)[0] << 8) | (*E)[1]) * Block();
	int64 End = int64(TotalUnits) * Block();
	for (int32 J = Idx + 1; J < Count; ++J)
	{
		const TArray<uint8>& F = Entries[J];
		if (!(F[0] & 0x80))
		{
			End = int64((F[0] << 8) | F[1]) * Block();
			break;
		}
	}
	const int64 Base = Page >= 0 ? int64(Page) * TotalUnits * Block() : 0;
	OutOffset = Base + Start;
	OutLength = int32(End - Start);
}

bool FSampleGroup::IsEnabled(int32 I) const
{
	return !bHasEnabled || (Enabled.IsValidIndex(I >> 3) && (Enabled[I >> 3] & (1 << (I & 7))));
}

bool FSampleGroup::PageAllows(uint32 Page, int32 I) const
{
	if (Page >= Pages)
	{
		return false;
	}
	const int64 Bit = int64(Count) * Page + I;
	return PageBits.IsValidIndex(int32(Bit >> 3)) && (PageBits[int32(Bit >> 3)] & (1 << (Bit & 7)));
}

int32 FSampleGroup::HistoryAge(int32 I) const
{
	if (!HistLen)
	{
		return 0;
	}
	int32 C = HistoryCursor;
	for (int32 Age = 0; Age < HistLen; ++Age)
	{
		C = C > 0 ? C - 1 : HistLen - 1;
		if (History.IsValidIndex(C) && History[C] == I)
		{
			return Age + 1;
		}
	}
	return 0;
}

void FSampleGroup::RecordPlay(int32 I)
{
	if (bHasEnabled && Enabled.IsValidIndex(I >> 3))
	{
		Enabled[I >> 3] &= uint8(~(1 << (I & 7)));
	}
	if (!HistLen)
	{
		return;
	}
	if (HistoryCursor < HistLen)
	{
		History[HistoryCursor] = uint8(I);
	}
	HistoryCursor = HistoryCursor + 1 < HistLen ? HistoryCursor + 1 : 0;
}

bool ReadBigF(const FString& Path, TArray<TPair<FString, TArray<uint8>>>& Out)
{
	TArray<uint8> Data;
	if (!FFileHelper::LoadFileToArray(Data, *Path) || Data.Num() < 16)
	{
		return false;
	}
	auto BE = [&Data](int64 P) { return uint32(Data[P] << 24 | Data[P + 1] << 16 | Data[P + 2] << 8 | Data[P + 3]); };
	const uint32 N = BE(8);
	int64 Pos = 16;
	for (uint32 I = 0; I < N && Pos + 8 < Data.Num(); ++I)
	{
		const uint32 Off = BE(Pos), Size = BE(Pos + 4);
		int64 End = Pos + 8;
		while (End < Data.Num() && Data[End] != 0) ++End;
		const FString Name = FString(int32(End - Pos - 8), reinterpret_cast<const ANSICHAR*>(Data.GetData() + Pos + 8));
		Pos = End + 1;
		if (int64(Off) + Size > Data.Num())
		{
			return false;
		}
		Out.Add({ Name, TArray<uint8>(Data.GetData() + Off, int32(Size)) });
	}
	return true;
}

bool LoadSection(const FString& Dir, const FString& Section, FEventDatabase& OutDb, TArray<FSampleGroup>& OutGroups, FString* OutError)
{
	TArray<TPair<FString, TArray<uint8>>> Arc;
	if (!ReadBigF(FPaths::Combine(Dir, TEXT("headers.big")), Arc))
	{
		if (OutError) *OutError = FString::Printf(TEXT("%s/headers.big missing (tools/convert_speech.py)"), *Dir);
		return false;
	}
	bool bEvt = false;
	for (const TPair<FString, TArray<uint8>>& E : Arc)
	{
		const FString Name = E.Key.Replace(TEXT("\\"), TEXT("/"));
		if (Name.EndsWith(TEXT(".evt"), ESearchCase::IgnoreCase))
		{
			if (!bEvt) bEvt = OutDb.Parse(E.Value);
		}
		else if (Name.EndsWith(TEXT(".hdr"), ESearchCase::IgnoreCase))
		{
			FSampleGroup G;
			if (G.Parse(E.Value, Name))
			{
				if (G.Section.IsEmpty()) G.Section = Section;
				OutGroups.Add(MoveTemp(G));
			}
		}
	}
	if (!bEvt && OutError) *OutError = FString::Printf(TEXT("%s: no Events.evt"), *Dir);
	return bEvt;
}

void FRng::SetSeed(uint32 Seed)
{
	static const int32 Offsets[6] = { -231928234, -2010044695, -970662609, 117621916, -1640677507, 1876900708 };
	const uint32 Base = Seed * 65537u;
	for (int32 K = 0; K < 6; ++K) S[K] = Base + uint32(Offsets[K]);
}

uint32 FRng::Next()
{
	uint32 S0 = S[0], S1 = S[1], S2 = S[2], S3 = S[3], S4 = S[4], S5 = S[5];
	const uint32 U1 = S5 + S4;
	const uint32 C = (U1 < S5 || U1 < S4) ? 1 : 0;
	const uint32 U2 = U1 + S3 + C;
	const uint32 U3 = U2 + S2 + (U2 < S3 ? 1 : 0);
	S5 = S5 + 1;
	const uint32 U4 = U3 + S1 + (U3 < S2 ? 1 : 0);
	S0 = U4 + S0 + (U4 < S1 ? 1 : 0);
	S1 = U4; S2 = U3; S3 = U2; S4 = U1;
	if (S5 == 0)
	{
		S4 = U1 + 1;
		if (S4 == 0)
		{
			S3 = U2 + 1;
			if (S3 == 0)
			{
				S2 = U3 + 1;
				if (S2 == 0)
				{
					S1 = U4 + 1;
					if (S1 == 0) S0 = S0 + 1;
				}
			}
		}
	}
	S[0] = S0; S[1] = S1; S[2] = S2; S[3] = S3; S[4] = S4; S[5] = S5;
	return S0;
}

void FNoRepeatRandom::Reset()
{
	for (int32 K = 0; K < 32; ++K) { HistValue[K] = 0xFFFF; HistTag[K] = 0xFFFF; }
	Cursor = 0;
}

int32 FNoRepeatRandom::Find(uint32 Value, int32 Depth, uint32 Tag) const
{
	Depth = FMath::Min(Depth, 32);
	int32 Seen = 0, C = Cursor;
	for (int32 K = 0; K < 33; ++K)
	{
		if (Seen >= Depth) return -1;
		if (HistTag[C] == Tag)
		{
			if (HistValue[C] == Value) return Seen;
			++Seen;
		}
		C = C > 0 ? C - 1 : 31;
	}
	return -1;
}

void FNoRepeatRandom::Record(uint32 Value, uint32 Tag)
{
	Cursor = Cursor < 31 ? Cursor + 1 : 0;
	HistValue[Cursor] = Value & 0xFFFF;
	HistTag[Cursor] = Tag & 0xFFFF;
}

uint32 FNoRepeatRandom::Draw(FRng& Rng, uint32 N, uint32 Tag)
{
	const int32 Depth = int32(FMath::Min<uint32>(N / 2, 10));
	int32 BestPos = -1;
	uint32 Best = Rng.Below(N);
	uint32 V = 0;
	for (int32 Tries = 1; Tries <= 32; ++Tries)
	{
		V = Rng.Below(N);
		const int32 Pos = Find(V, Depth, Tag & 0xFFFF);
		if (Pos == -1)
		{
			Record(V, Tag);
			return V;
		}
		if (Pos > BestPos) { BestPos = Pos; Best = V; }
		if (Tries >= 32)
		{
			Record(Best, Tag);
			return V;   // engine quirk: the last draw is returned
		}
	}
	return V;
}

FEngine::FEngine(uint32 Seed) : Rng(Seed)
{
	Reset(Seed);
}

void FEngine::Reset(uint32 Seed)
{
	Rng.SetSeed(Seed);
	Nrr.Reset();
	for (FSlot& S : Slots) S = FSlot();
	for (int32 C = 0; C < NumSpeechChannels; ++C)
	{
		Head[C] = -1; LastPosted[C] = -1; Pending[C] = 0; LastEventId[C] = -1; LastEventCount[C] = 0;
	}
	Seq = 0; LastTime = 0; AgreeKey = -1; AgreeBits = 0; bMustPlayQueued = false;
	PreparedChannel = -1;
	Prepared.Reset();
}

void FEngine::Register(const FEventDatabase& Db, const TArray<FSampleGroup>& InGroups, int32 Channel)
{
	const int32 Ch = Channel >= 0 ? Channel : ChannelOfDb(Db.Db);
	bool bFound = false;
	for (FDbEntry& E : Dbs)
	{
		if (E.Db.Db == Db.Db) { E.Db = Db; E.Channel = Ch; bFound = true; }
	}
	if (!bFound) Dbs.Add({ Db, Ch });
	for (FSlot& S : Slots) S = FSlot();
	Groups.Append(InGroups);
}

const FEventDatabase* FEngine::FindDb(int32 Db) const
{
	for (const FDbEntry& E : Dbs) if (E.Db.Db == Db) return &E.Db;
	return nullptr;
}

const FEvent* FEngine::EventOf(uint32 Req0, int32& OutChannel) const
{
	const int32 Db = (Req0 >> 24) & 0xFF;
	for (const FDbEntry& E : Dbs)
	{
		if (E.Db.Db == Db)
		{
			OutChannel = E.Channel;
			return E.Db.Find(uint16(Req0 & 0xFFFF));
		}
	}
	return nullptr;
}

int32 FEngine::GroupIndex(uint16 Gid, int32 Sub) const
{
	for (int32 I = 0; I < Groups.Num(); ++I)
	{
		if (Groups[I].Id == Gid && (Sub < 0 || Groups[I].Sub == Sub)) return I;
	}
	return -1;
}

bool FEngine::Post(const FRequest& InReq)
{
	FRequest Req = InReq;
	while (Req.Num() < RequestWords) Req.Add(0);
	int32 Ch = 0;
	const FEvent* Ev = EventOf(Req[0], Ch);
	if (!Ev || Ch < 0 || Ch >= NumSpeechChannels)
	{
		return false;
	}
	const bool bFollow = (Ev->Flags & FlagFollowOn) != 0;
	uint16 Priority = 0;
	if (!bFollow)
	{
		if (Ev->Chance < int32(Nrr.Draw(Rng, 100, 0xFFFF)))
		{
			return false;
		}
		Priority = Ev->Priority;
	}
	else
	{
		const int32 H = Head[Ch], Lp = LastPosted[Ch];
		if (!(H >= 0 && H < NumSlots && Lp >= 0 && Lp < NumSlots) || !Slots[H].bUsed)
		{
			return false;
		}
		Priority = Slots[H].Event->Priority;
	}
	const int32 S = TakeSlot(Priority, Ch, bFollow);
	if (S < 0)
	{
		if (bFollow) Free(Head[Ch]);
		return false;
	}
	const uint32 Now = Clock();
	Seq = Now == LastTime ? Seq + 1 : 0;
	LastTime = Now;
	FSlot& Sl = Slots[S];
	Sl.bUsed = true; Sl.Event = Ev; Sl.Channel = Ch; Sl.Time = Now; Sl.Seq = Seq; Sl.bFollow = bFollow; Sl.Next = -1; Sl.Req = Req;
	if (bFollow)
	{
		if (LastPosted[Ch] >= 0 && Slots[LastPosted[Ch]].bUsed) Slots[LastPosted[Ch]].Next = S;
	}
	else
	{
		++Pending[Ch];
		Head[Ch] = S;
	}
	LastPosted[Ch] = S;
	return true;
}

int32 FEngine::TakeSlot(uint16 Priority, int32 Ch, bool bFollow)
{
	for (int32 I = 0; I < NumSlots; ++I)
	{
		if (!Slots[I].bUsed) return I;
	}
	const int32 Protect = bFollow ? Head[Ch] : -1;
	const uint32 Now = Clock();
	for (int32 I = 0; I < NumSlots; ++I)
	{
		const FSlot& Sl = Slots[I];
		const uint32 T = Sl.Event->Timeout;
		if (!Sl.bFollow && T && (Now - Sl.Time) > T && I != Protect)
		{
			Free(I);
			return I;
		}
	}
	for (int32 I = 0; I < NumSlots; ++I)
	{
		FSlot& Sl = Slots[I];
		if (!Sl.bFollow && Sl.Event->Priority <= Priority && Sl.Channel == Ch && I != Protect)
		{
			--Pending[Ch];
			Sl = FSlot();
			return I;
		}
	}
	return -1;
}

void FEngine::Free(int32 I)
{
	if (I < 0 || I >= NumSlots || !Slots[I].bUsed || Slots[I].bFollow)
	{
		return;
	}
	const int32 Ch = Slots[I].Channel;
	int32 N = Slots[I].Next;
	Slots[I] = FSlot();
	if (Head[Ch] == I) Head[Ch] = -1;
	if (LastPosted[Ch] == I) LastPosted[Ch] = -1;
	while (N >= 0 && N < NumSlots && Slots[N].bUsed && Slots[N].bFollow)
	{
		if (LastPosted[Ch] == N) LastPosted[Ch] = -1;
		const int32 Nxt = Slots[N].Next;
		Slots[N] = FSlot();
		N = Nxt;
	}
	Pending[Ch] = FMath::Max(0, Pending[Ch] - 1);
}

int32 FEngine::Pick(int32 Ch, const TArray<uint16>* Allowed)
{
	const uint32 Now = Clock();
	int32 Best = -1;
	uint32 BestPri = 0, BestAge = MAX_uint32, BestSeq = 0;
	for (int32 I = 0; I < NumSlots; ++I)
	{
		const FSlot& Sl = Slots[I];
		if (!Sl.bUsed || Sl.Channel != Ch || Sl.bFollow) continue;
		const FEvent& Ev = *Sl.Event;
		const bool bOk = !Allowed || Allowed->Contains(Ev.Id);
		const uint32 Age = Now - Sl.Time;
		if (Ev.Timeout && Age > Ev.Timeout)
		{
			Free(I);
			continue;
		}
		if (!bOk) continue;
		if (Ev.Priority > BestPri)
		{
			Best = I; BestPri = Ev.Priority; BestAge = Age; BestSeq = Sl.Seq;
		}
		else if (Ev.Priority == BestPri && (Age < BestAge || (Age == BestAge && Sl.Seq > BestSeq)))
		{
			Best = I; BestAge = Age; BestSeq = Sl.Seq;
		}
	}
	return Best;
}

int32 FEngine::PeekPick(int32 Channel)
{
	return Pick(Channel, nullptr);
}

const FEvent* FEngine::FindEventOnChannel(int32 Ch, int32 Id) const
{
	for (const FDbEntry& E : Dbs)
	{
		if (E.Channel == Ch)
		{
			if (const FEvent* Ev = E.Db.Find(uint16(Id))) return Ev;
		}
	}
	return nullptr;
}

TArray<FLine> FEngine::Service(int32 Ch)
{
	if (Ch < 0 || Ch >= NumSpeechChannels)
	{
		return {};
	}
	if (PreparedChannel == Ch && Prepared.Num() > 0)
	{
		return Play(Ch);
	}
	if (!Pending[Ch])
	{
		return {};
	}
	const TArray<uint16>* Allowed = nullptr;
	if (LastEventId[Ch] >= 0)
	{
		const FEvent* Last = FindEventOnChannel(Ch, LastEventId[Ch]);
		if (Last && Last->bFollowList && Last->FollowList.Num() > 0)
		{
			Allowed = &Last->FollowList;
		}
	}
	int32 S = Pick(Ch, Allowed);
	if (S < 0 && Allowed)
	{
		S = Pick(Ch, nullptr);
	}
	if (S < 0)
	{
		return {};
	}
	DropOlder(S);
	PreparedChannel = -1;
	Prepared.Reset();
	bool bOk = false;
	int32 I = S;
	while (true)
	{
		FLine Line;
		bOk = Resolve(Slots[I].Req, Line);
		if (bOk && (PreparedChannel < 0 || PreparedChannel == Ch) && Prepared.Num() < MaxLines)
		{
			PreparedChannel = Ch;
			Prepared.Add(MoveTemp(Line));
		}
		const int32 Nxt = Slots[I].Next;
		if (Nxt == -1 || !bOk) break;
		I = Nxt;
	}
	Free(S);
	return bOk ? Play(Ch) : TArray<FLine>();
}

TArray<FLine> FEngine::Play(int32 Ch)
{
	TArray<FLine> Lines;
	if (PreparedChannel == Ch)
	{
		Lines = MoveTemp(Prepared);
	}
	Prepared.Reset();
	PreparedChannel = -1;
	for (int32 K = 0; K < Lines.Num(); ++K)
	{
		for (const FResolvedSample& Smp : Lines[K].Samples)
		{
			Groups[Smp.Group].RecordPlay(Smp.Sample);
		}
		if (K == 0)
		{
			const int32 Eid = Lines[K].Event->Id;
			LastEventCount[Ch] = LastEventId[Ch] == Eid ? LastEventCount[Ch] + 1 : 1;
			LastEventId[Ch] = Eid;
		}
	}
	return Lines;
}

void FEngine::DropOlder(int32 S)
{
	const FSlot& Sl = Slots[S];
	bMustPlayQueued = false;
	for (int32 I = 0; I < NumSlots; ++I)
	{
		const FSlot& O = Slots[I];
		if (I == S || !O.bUsed || O.Channel != Sl.Channel || O.bFollow) continue;
		const bool bOlder = O.Time < Sl.Time || (O.Time == Sl.Time && O.Seq < Sl.Seq);
		if (bOlder)
		{
			if (!(O.Event->Flags & FlagKeepOnNew)) Free(I);
		}
		else if (O.Event->Chance == 100)
		{
			bMustPlayQueued = true;
		}
	}
}

bool FEngine::Resolve(const FRequest& InReq, FLine& Out)
{
	FRequest Req = InReq;
	while (Req.Num() < RequestWords) Req.Add(0);
	int32 Ch = 0;
	const FEvent* EvP = EventOf(Req[0], Ch);
	if (!EvP)
	{
		return false;
	}
	const FEvent& Ev = *EvP;
	if (Ch >= 0 && Ch < NumSpeechChannels && LastEventId[Ch] == Ev.Id && 0 < Ev.MaxRepeats && Ev.MaxRepeats <= LastEventCount[Ch])
	{
		return false;
	}
	int32 Mode = 0;
	if (bMustPlayQueued) Mode = FMath::Min(Mode + 1, 2);
	if (!(Ev.Flags & FlagCanShorten) && Mode == 1) Mode = 0;
	const FEventDatabase* Db = FindDb((Req[0] >> 24) & 0xFF);
	const int32 Limit = Mode && Db ? Db->ShortenLimit : 0;
	uint32 Ctx = 0, Unknown = 0;
	Context(Ev, Req, Ctx, Unknown);
	const TArray<int32> Order = VariantOrder(Ev);
	uint32 PMask = 0;
	if (Ev.Flags & FlagParamMask)
	{
		for (int32 W = 0; W < 16; ++W)
		{
			const uint32 Sel = (Ev.ParamMaskSelector >> ((W & 15) * 2)) & 3;
			const uint32 V = Req[W + 1];
			if ((Sel == 1 && V != 0) || (Sel == 2 && V != MAX_uint32)) PMask |= 1u << W;
		}
	}
	for (const int32 Vi : Order)
	{
		if (Vi >= Ev.Variants.Num()) break;
		const FVariant& Var = Ev.Variants[Vi];
		if ((Ev.Flags & FlagParamMask) && Ev.VariantParamMasks[Vi] != PMask) continue;
		if (Var.NumSegments >= 13 || !Var.ModeOk(Mode)) continue;
		if (((Var.CtxValue ^ Ctx) & Var.CtxMask & ~Unknown) & 0xFF) continue;
		TArray<FChosen> Segs;
		TArray<int32> List;
		if (!Candidates(Var, Req, Segs, List)) continue;
		TArray<int32> Picks;
		const bool bPicked = Mode == 1 ? Shorten(Segs, List, Limit, Picks) : PickSamples(Segs, List, Picks);
		if (!bPicked) continue;
		Out = FLine();
		Out.Request = Req;
		Out.Event = &Ev;
		Out.Variant = Vi;
		for (int32 K = 0; K < Segs.Num(); ++K)
		{
			FResolvedSample Smp;
			Smp.Group = Segs[K].GroupIndex;
			Smp.Sample = Picks[K];
			Groups[Smp.Group].Span(Smp.Sample, int32(Segs[K].Page), Smp.Offset, Smp.Length);
			Out.Samples.Add(Smp);
		}
		return true;
	}
	return false;
}

void FEngine::Context(const FEvent& Ev, const FRequest& Req, uint32& OutCtx, uint32& OutUnknown) const
{
	OutCtx = OutUnknown = 0;
	for (int32 K = 0; K < Ev.Callbacks.Num(); ++K)
	{
		const uint8 Idx = Ev.Callbacks[K][1], Kind = Ev.Callbacks[K][2];
		if (Idx >= RequestWords) continue;
		const uint32 Bit = (1u << (7 - K)) & 0xFF;
		if (Kind == 4)
		{
			if (Req[Idx] != 0) OutCtx |= Bit;
		}
		else
		{
			OutUnknown |= Bit;
		}
	}
}

TArray<int32> FEngine::VariantOrder(const FEvent& Ev)
{
	TArray<int64> W;
	int64 Total = 0;
	for (const FVariant& V : Ev.Variants) { W.Add(V.Weight()); Total += V.Weight(); }
	TArray<int32> Order;
	while (Total > 0)
	{
		int64 R = int64(Nrr.Draw(Rng, uint32(Total), 0xFFFF));
		int32 K = 0;
		R -= W[0];
		while (R >= 0 && K + 1 < W.Num())
		{
			++K;
			R -= W[K];
		}
		Order.Add(K);
		Total -= W[K];
		W[K] = 0;
	}
	for (int32 I = 0; I < Ev.Variants.Num(); ++I)
	{
		if (Ev.Variants[I].Weight() == 0) Order.Add(I);
	}
	return Order;
}

bool FEngine::Candidates(const FVariant& Var, const FRequest& Req, TArray<FChosen>& OutSegs, TArray<int32>& OutList)
{
	for (const FSegment& Seg : Var.Segments)
	{
		int32 Gi = -1;
		int64 Page = -1;
		if (Seg.Kind == 1)
		{
			Gi = Seg.ParamIndex < RequestWords ? GroupIndex(Seg.GroupId, int32(Req[Seg.ParamIndex] & 0xFFFF)) : -1;
		}
		else if (Seg.Kind == 0)
		{
			Gi = GroupIndex(Seg.GroupId);
		}
		else if (Seg.Kind == 2)
		{
			Gi = GroupIndex(Seg.GroupId);
			const uint32 V = Seg.ParamIndex < RequestWords ? Req[Seg.ParamIndex] : 0;
			Page = (Gi >= 0 && Groups[Gi].Pages != 0xFFFF && V < Groups[Gi].Pages) ? int64(V) : -1;
			if (Page < 0) Gi = -1;
		}
		if (Gi < 0)
		{
			return false;
		}
		FChosen Cs;
		Cs.GroupIndex = Gi;
		Cs.Page = Page;
		Cs.ListStart = OutList.Num();
		Cs.Seg = &Seg;
		const FSampleGroup& G = Groups[Gi];
		int32 Fresh = 0, Oldest = -1, OldestAge = 0;
		for (int32 I = 0; I < G.Count; ++I)
		{
			if (!ConditionsOk(G, I, Seg, Req)) continue;
			if (Page >= 0 && !G.PageAllows(uint32(Page), I)) continue;
			if (!G.IsEnabled(I)) continue;
			const int32 Age = G.HistoryAge(I);
			if (Age < 1)
			{
				if (OutList.Num() < 100) { OutList.Add(I); ++Fresh; }
			}
			else if (Age > OldestAge)
			{
				Oldest = I; OldestAge = Age;
			}
		}
		if (Fresh == 0 && Oldest != -1 && OutList.Num() < 100)
		{
			OutList.Add(Oldest);
			Fresh = 1;
		}
		if (Fresh == 0)
		{
			return false;
		}
		Cs.ListCount = Fresh;
		OutSegs.Add(Cs);
	}
	return true;
}

bool FEngine::ConditionsOk(const FSampleGroup& G, int32 I, const FSegment& Seg, const FRequest& Req)
{
	if (Seg.NumCond >= 5)
	{
		return false;
	}
	for (int32 J = 0; J < FMath::Max<int32>(Seg.NumCond, 0); ++J)
	{
		const uint8 Tag = G.Tag(I, J);
		if (Tag > 31) return false;
		const uint32 Bit = 1u << Tag;
		if (!(Bit & Seg.CondMask[J])) return false;
		const uint8 P = Seg.CondParam[J];
		if (P != 0 && P != 0xFE && P != 0xFF && !(Bit & (P < RequestWords ? Req[P] : 0u))) return false;
	}
	return true;
}

bool FEngine::PickSamples(TArray<FChosen>& Segs, TArray<int32>& List, TArray<int32>& OutPicks)
{
	for (FChosen& Cs : Segs)
	{
		const FSampleGroup& G = Groups[Cs.GroupIndex];
		const FSegment& Seg = *Cs.Seg;
		int32 Chosen = -1;
		bool bSpecial = false;
		for (int32 J = 0; J < FMath::Max<int32>(Seg.NumCond, 0); ++J)
		{
			const uint8 Code = Seg.CondParam[J];
			if (Code == 0xFE)
			{
				bSpecial = true;
				const uint32 R = Nrr.Draw(Rng, uint32(Cs.ListCount), uint32(Cs.GroupIndex));
				Chosen = List[Cs.ListStart + int32(R)];
				const int32* Key = G.Keys.Find(J);
				const uint8 Tag = G.Tag(Chosen, J);
				AgreeKey = Key ? *Key : -1;
				AgreeBits = Tag == 0xFF ? 0u : 1u << (Tag & 31);
				break;
			}
			if (Code == 0xFF)
			{
				bSpecial = true;
				const int32* Key = G.Keys.Find(J);
				const uint32 Bits = AgreeKey == (Key ? *Key : -1) ? AgreeBits : 0u;
				TArray<int32> Order;
				for (int32 K = 0; K < Cs.ListCount; ++K) Order.Add(List[Cs.ListStart + K]);
				Shuffle(Order);
				for (int32 K = 0; K < Cs.ListCount; ++K) List[Cs.ListStart + K] = Order[K];
				for (const int32 Smp : Order)
				{
					const uint8 Tag = G.Tag(Smp, J);
					const uint32 B = Tag == 0xFF ? 0u : 1u << (Tag & 31);
					if (B & Bits) Chosen = Smp;
				}
				if (Chosen >= 0) break;
			}
		}
		if (Chosen < 0)
		{
			if (bSpecial) return false;
			Chosen = List[Cs.ListStart + int32(Nrr.Draw(Rng, uint32(Cs.ListCount), uint32(Cs.GroupIndex)))];
		}
		OutPicks.Add(Chosen);
	}
	return true;
}

void FEngine::Shuffle(TArray<int32>& Items)
{
	const int32 N = Items.Num();
	for (int32 I = 0; I < N / 2; ++I)
	{
		const int32 J = int32(Nrr.Draw(Rng, uint32(N), uint32(N)));
		Items.Swap(I, J);
	}
}

bool FEngine::Shorten(TArray<FChosen>& Segs, TArray<int32>& List, int32 Limit, TArray<int32>& OutPicks)
{
	for (FChosen& Cs : Segs)
	{
		const int32 N = Cs.ListCount;
		for (int32 I = 0; I < N / 2; ++I)
		{
			const int32 J = int32(Nrr.Draw(Rng, uint32(N), uint32(Cs.GroupIndex)));
			List.Swap(Cs.ListStart + I, Cs.ListStart + J);
		}
		Cs.Pick = 0;
	}
	while (true)
	{
		int64 Total = 0;
		for (const FChosen& Cs : Segs)
		{
			int64 Off = 0;
			int32 Len = 0;
			Groups[Cs.GroupIndex].Span(List[Cs.ListStart + Cs.Pick], -1, Off, Len);
			Total += Len;
		}
		if (Total * 100 / BytesPerSecond < Limit)
		{
			for (const FChosen& Cs : Segs) OutPicks.Add(List[Cs.ListStart + Cs.Pick]);
			return true;
		}
		int32 K = Segs.Num() - 1;
		while (K >= 0)
		{
			if (++Segs[K].Pick < Segs[K].ListCount) break;
			Segs[K].Pick = 0;
			--K;
		}
		if (K < 0) return false;
	}
}

uint32 LanguageMask(int32 Language)
{
	switch (Language)
	{
	case 2: return 2;
	case 3: return 8;
	case 4: return 4;
	default: return 1;
	}
}

FString LanguageSuffix(int32 Language)
{
	static const TCHAR* S[5] = { TEXT("_ENG"), TEXT("_ENG"), TEXT("_GER"), TEXT("_FRE"), TEXT("_JAP") };
	return S[FMath::Clamp(Language, 0, 4)];
}

TArray<FRequest> NarrationRequests(const TArray<int32>& Script, int32 Language)
{
	const uint32 Lang = LanguageMask(Language);
	TArray<FRequest> Out;
	for (const int32 V : Script)
	{
		if (V < 1000) continue;
		if (V < 2000) Out.Add(MakeRequest({ RequestId(DbNarr, 0x8002), uint32(V - 1000), Lang }));
		else if (V < 3000) Out.Add(MakeRequest({ RequestId(DbNarr, 0x8004), uint32(V - 2000), Lang }));
		else if (V < 4000) Out.Add(MakeRequest({ RequestId(DbNarr, 0x8003), uint32(V - 3000), Lang }));
		else if (V < 5000) Out.Add(MakeRequest({ RequestId(DbNarr, 0x8005), uint32(V - 4000), Lang }));
		else if (V < 6000) Out.Add(MakeRequest({ RequestId(DbNarr, 0x8015), 1u << ((V - 5001) & 31) }));
		else if (V < 7000) Out.Add(MakeRequest({ RequestId(DbNarr, 0x8016), uint32(V - 6000), Lang }));
	}
	return Out;
}

TMap<int32, TArray<int32>> ParseNascript(const FString& Text, const FString& Section, const FString& Suffix)
{
	TMap<int32, TArray<int32>> Scripts;
	const FString A = Section.ToUpper(), B = (Section + Suffix).ToUpper();
	FString Cur;
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, false);
	for (const FString& Raw : Lines)
	{
		FString Line = Raw;
		int32 Hash = INDEX_NONE;
		if (Line.FindChar(TEXT('#'), Hash)) Line = Line.Left(Hash);
		Line.TrimStartAndEndInline();
		if (Line.IsEmpty()) continue;
		if (Line.StartsWith(TEXT("[")))
		{
			Cur = Line.Replace(TEXT("["), TEXT("")).Replace(TEXT("]"), TEXT("")).TrimStartAndEnd().ToUpper();
			continue;
		}
		FString Key, Val;
		if ((Cur != A && Cur != B) || !Line.Split(TEXT("="), &Key, &Val)) continue;
		Val.TrimStartAndEndInline();
		Val = Val.Replace(TEXT("\""), TEXT(""));
		TArray<FString> Toks;
		Val.ParseIntoArray(Toks, TEXT(","));
		TArray<int32> Items;
		for (FString T : Toks)
		{
			T.TrimStartAndEndInline();
			if (T.IsEmpty()) continue;
			Items.Add((T == TEXT("b") || T == TEXT("B")) ? -1 : FCString::Atoi(*T));
		}
		Scripts.Add(FCString::Atoi(*Key.TrimStartAndEnd()), Items);
	}
	return Scripts;
}

FString NascriptVenueFor(const FString& Text, const FString& Track)
{
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, true);
	const FString T = Track.ToUpper();
	for (const FString& Raw : Lines)
	{
		FString L = Raw.TrimStartAndEnd();
		if (!L.StartsWith(TEXT("[")) || L.Contains(TEXT("_"))) continue;
		L = L.Replace(TEXT("["), TEXT("")).Replace(TEXT("]"), TEXT("")).TrimStartAndEnd().ToUpper();
		if (!L.IsEmpty() && T.Contains(L)) return L;
	}
	return FString();
}
}
