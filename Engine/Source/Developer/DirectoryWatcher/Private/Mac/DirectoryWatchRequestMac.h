// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#ifndef _INC_DIRECTORYWATCHREQUESTMAC
#define _INC_DIRECTORYWATCHREQUESTMAC

#include <CoreServices/CoreServices.h>

class FDirectoryWatchRequestMac
{
public:
	FDirectoryWatchRequestMac(bool bInIncludeDirectoryEvents);
	virtual ~FDirectoryWatchRequestMac();

	/** Sets up the directory handle and request information */
	bool Init(const FString& InDirectory);

	/** Adds a delegate to get fired when the directory changes */
	FDelegateHandle AddDelegate( const IDirectoryWatcher::FDirectoryChanged& InDelegate );
	/** Removes a delegate to get fired when the directory changes */
	DELEGATE_DEPRECATED("This overload of RemoveDelegate is deprecated, instead pass the result of AddDelegate.")
	bool RemoveDelegate( const IDirectoryWatcher::FDirectoryChanged& InDelegate );
	/** Same as above, but for use within other deprecated calls to prevent multiple deprecation warnings */
	bool DEPRECATED_RemoveDelegate( const IDirectoryWatcher::FDirectoryChanged& InDelegate );
	/** Removes a delegate to get fired when the directory changes */
	bool RemoveDelegate( FDelegateHandle InHandle );
	/** Returns true if this request has any delegates listening to directory changes */
	bool HasDelegates() const;
	/** Prepares the request for deletion */
	void EndWatchRequest();
	/** Triggers all pending file change notifications */
	void ProcessPendingNotifications();

private:

	FSEventStreamRef	EventStream;
	bool				bRunning;
	bool				bEndWatchRequestInvoked;
	bool				bIncludeDirectoryEvents;

	TArray<IDirectoryWatcher::FDirectoryChanged> Delegates;
	TArray<FFileChangeData> FileChanges;

	friend void DirectoryWatchMacCallback( ConstFSEventStreamRef StreamRef, void* WatchRequestPtr, size_t EventCount, void* EventPaths, const FSEventStreamEventFlags EventFlags[], const FSEventStreamEventId EventIDs[] );

	void ProcessChanges( size_t EventCount, void* EventPaths, const FSEventStreamEventFlags EventFlags[] );
	void Shutdown( void );
};

#endif // _INC_DIRECTORYWATCHREQUESTMAC
