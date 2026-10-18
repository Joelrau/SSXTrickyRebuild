#pragma once

#include "CoreMinimal.h"

namespace SSXSpeech
{
	constexpr int32 DbFE = 0, DbMC = 1, DbChar = 3, DbNarr = 4, DbAnim = 5;
	constexpr int32 NumSlots = 16, RequestWords = 20, MaxLines = 8, NumSpeechChannels = 2;
	constexpr int32 BytesPerSecond = 22050 * 16 / 8 / 10;
	constexpr uint8 FlagCanShorten = 1, FlagKeepOnNew = 4, FlagParamMask = 8, FlagFollowList = 16, FlagFollowOn = 32;

	SSXTRICKYREBUILD_API int32 ChannelOfDb(int32 Db);
	inline uint32 RequestId(int32 Db, int32 EventId) { return (uint32(Db) << 24) | uint32(EventId); }

	using FRequest = TArray<uint32, TFixedAllocator<RequestWords>>;
	SSXTRICKYREBUILD_API FRequest MakeRequest(std::initializer_list<uint32> Words);

	struct FSegment
	{
		uint16 GroupId = 0;
		uint8 ParamIndex = 0;
		uint8 Kind = 0;
		int8 NumCond = 0;
		uint8 CondParam[4] = {};
		uint32 CondMask[4] = {};
	};

	struct FVariant
	{
		uint8 WeightByte = 0, CtxMask = 0, CtxValue = 0, ModeRule = 0;
		int32 NumSegments = 0;
		TArray<FSegment> Segments;
		int64 Weight() const;
		bool ModeOk(int32 Mode) const;
	};

	struct FEvent
	{
		uint16 Id = 0, Timeout = 0, Priority = 0;
		int8 MaxRepeats = 0, Chance = 0;
		uint8 Flags = 0;
		TArray<FVariant> Variants;
		TArray<TArray<uint8>> Callbacks;
		uint32 ParamMaskSelector = 0;
		TArray<uint16> VariantParamMasks;
		bool bFollowList = false;
		TArray<uint16> FollowList;
	};

	struct SSXTRICKYREBUILD_API FEventDatabase
	{
		uint8 Db = 0, Sub = 0;
		uint16 ShortenLimit = 0, InterruptPriority = 0;
		TArray<FEvent> Events;
		bool Parse(TConstArrayView<uint8> Blob);
		const FEvent* Find(uint16 Id) const;
	};

	struct SSXTRICKYREBUILD_API FSampleGroup
	{
		FString Path;
		FString Section, DatStem;
		uint16 Id = 0, Sub = 0;
		uint8 Flags = 0, Count = 0, HistLen = 0, Shift = 0;
		uint16 TotalUnits = 0, Pages = 0;
		int32 NumTags = 0;
		TArray<TArray<uint8>> Entries;        // per sample: 2 offset bytes + tag bytes
		TMap<int32, int32> Keys;
		bool bHasEnabled = false;
		TArray<uint8> Enabled;
		uint8 HistoryCursor = 0;
		TArray<uint8> History;
		TArray<uint8> PageBits;

		bool Parse(TConstArrayView<uint8> Blob, const FString& InPath);
		int32 Block() const { return 256 * (Shift + 1); }
		uint8 Tag(int32 I, int32 J) const { return J < NumTags ? Entries[I][2 + J] : 0xFF; }
		// (byte offset into the .dat, byte length); paged groups add page * total size.
		void Span(int32 I, int32 Page, int64& OutOffset, int32& OutLength) const;
		bool IsEnabled(int32 I) const;
		bool PageAllows(uint32 Page, int32 I) const;
		int32 HistoryAge(int32 I) const;
		void RecordPlay(int32 I);
	};

	// headers.big (BIGF, big-endian directory).
	SSXTRICKYREBUILD_API bool ReadBigF(const FString& Path, TArray<TPair<FString, TArray<uint8>>>& Out);
	SSXTRICKYREBUILD_API bool LoadSection(const FString& Dir, const FString& Section, FEventDatabase& OutDb, TArray<FSampleGroup>& OutGroups, FString* OutError = nullptr);

	class SSXTRICKYREBUILD_API FRng
	{
	public:
		explicit FRng(uint32 Seed = 0) { SetSeed(Seed); }
		void SetSeed(uint32 Seed);
		void SetState(const uint32 InState[6]) { FMemory::Memcpy(S, InState, sizeof(S)); }
		uint32 Next();
		uint32 Below(uint32 N) { return uint32((uint64(Next() >> 16) * N) >> 16); }
		uint32 S[6] = {};
	};

	class SSXTRICKYREBUILD_API FNoRepeatRandom
	{
	public:
		FNoRepeatRandom() { Reset(); }
		void Reset();
		uint32 Draw(FRng& Rng, uint32 N, uint32 Tag);
	private:
		int32 Find(uint32 Value, int32 Depth, uint32 Tag) const;
		void Record(uint32 Value, uint32 Tag);
		uint32 HistValue[32];
		uint32 HistTag[32];
		int32 Cursor = 0;
	};

	struct FResolvedSample
	{
		int32 Group = -1;
		int32 Sample = -1;
		int64 Offset = 0;
		int32 Length = 0;
	};

	struct FLine
	{
		FRequest Request;
		const FEvent* Event = nullptr;
		int32 Variant = -1;
		TArray<FResolvedSample> Samples;
	};

	class SSXTRICKYREBUILD_API FEngine
	{
	public:
		explicit FEngine(uint32 Seed = 0);
		void Reset(uint32 Seed);
		void Clear(uint32 Seed = 0) { Dbs.Reset(); Groups.Reset(); Reset(Seed); }
		void Register(const FEventDatabase& Db, const TArray<FSampleGroup>& InGroups, int32 Channel = -1);
		bool Post(const FRequest& Request);
		/** Dispatch the best pending request of the channel (and its follow-on chain); returns the lines handed to the player (empty if none). */
		TArray<FLine> Service(int32 Channel);
		bool Resolve(const FRequest& Req, FLine& Out);

		const TArray<FSampleGroup>& GetGroups() const { return Groups; }
		const FEventDatabase* FindDb(int32 Db) const;
		int32 PendingCount(int32 Channel) const { return Pending[Channel]; }
		int32 PeekPick(int32 Channel);
		const FEvent* SlotEvent(int32 Slot) const { return Slots[Slot].bUsed ? Slots[Slot].Event : nullptr; }
		int32 SlotNext(int32 Slot) const { return Slots[Slot].Next; }
		int32 GetHead(int32 Channel) const { return Head[Channel]; }
		void SetLastEvent(int32 Channel, int32 Id, int32 Count) { LastEventId[Channel] = Id; LastEventCount[Channel] = Count; }
		FRng& GetRng() { return Rng; }

	private:
		struct FSlot
		{
			bool bUsed = false;
			const FEvent* Event = nullptr;
			int32 Channel = 0;
			uint32 Time = 0;
			uint32 Seq = 0;
			bool bFollow = false;
			int32 Next = -1;
			FRequest Req;
		};
		struct FChosen
		{
			int32 GroupIndex = -1;
			int64 Page = -1;
			int32 ListStart = 0, ListCount = 0, Pick = 0;
			const FSegment* Seg = nullptr;
		};
		struct FDbEntry { FEventDatabase Db; int32 Channel = 0; };

		uint32 Clock() const { return 0; }
		const FEvent* EventOf(uint32 Req0, int32& OutChannel) const;
		int32 GroupIndex(uint16 Gid, int32 Sub = -1) const;
		int32 TakeSlot(uint16 Priority, int32 Ch, bool bFollow);
		void Free(int32 I);
		int32 Pick(int32 Ch, const TArray<uint16>* Allowed);
		TArray<FLine> Play(int32 Ch);
		const FEvent* FindEventOnChannel(int32 Ch, int32 Id) const;
		void DropOlder(int32 S);
		void Context(const FEvent& Ev, const FRequest& Req, uint32& OutCtx, uint32& OutUnknown) const;
		TArray<int32> VariantOrder(const FEvent& Ev);
		bool Candidates(const FVariant& Var, const FRequest& Req, TArray<FChosen>& OutSegs, TArray<int32>& OutList);
		static bool ConditionsOk(const FSampleGroup& G, int32 I, const FSegment& Seg, const FRequest& Req);
		bool PickSamples(TArray<FChosen>& Segs, TArray<int32>& List, TArray<int32>& OutPicks);
		void Shuffle(TArray<int32>& Items);
		bool Shorten(TArray<FChosen>& Segs, TArray<int32>& List, int32 Limit, TArray<int32>& OutPicks);

		FRng Rng;
		FNoRepeatRandom Nrr;
		TArray<FDbEntry> Dbs;
		TArray<FSampleGroup> Groups;
		FSlot Slots[NumSlots];
		int32 Head[NumSpeechChannels];
		int32 LastPosted[NumSpeechChannels];
		int32 Pending[NumSpeechChannels];
		int32 LastEventId[NumSpeechChannels];
		int32 LastEventCount[NumSpeechChannels];
		uint32 Seq = 0, LastTime = 0;
		int32 AgreeKey = -1;
		uint32 AgreeBits = 0;
		bool bMustPlayQueued = false;
		int32 PreparedChannel = -1;
		TArray<FLine> Prepared;
	};

	SSXTRICKYREBUILD_API uint32 LanguageMask(int32 Language);
	SSXTRICKYREBUILD_API TArray<FRequest> NarrationRequests(const TArray<int32>& Script, int32 Language);
	SSXTRICKYREBUILD_API TMap<int32, TArray<int32>> ParseNascript(const FString& Text, const FString& Section, const FString& LanguageSuffix);
	SSXTRICKYREBUILD_API FString NascriptVenueFor(const FString& Text, const FString& Track);
	SSXTRICKYREBUILD_API FString LanguageSuffix(int32 Language);
	inline FRequest AnimRequest(int32 Value) { return MakeRequest({ RequestId(DbAnim, 0xA000), uint32(Value) }); }
}
