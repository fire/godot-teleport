/************************************************************************************

Filename    :   MessageQueue.cpp
Content     :   Thread communication by string commands
Created     :   October 15, 2013
Authors     :   John Carmack

Copyright   :   Copyright (c) Facebook Technologies, LLC and its affiliates. All rights reserved.

*************************************************************************************/

#include "MessageQueue.h"

#include <stdlib.h>
#include <stdio.h>

#include "OVR_LogUtils.h"

namespace OVR
{

bool ovrMessageQueue::debug = false;

ovrMessageQueue::ovrMessageQueue( int maxMessages_ ) :
	shutdown( false ),
	maxMessages( maxMessages_ ),
	messages( new message_t[ maxMessages_ ] ),
	head( 0 ),
	tail( 0 ),
	synced( false )
{
	OVR_ASSERT( maxMessages > 0 );

	for ( int i = 0; i < maxMessages; i++ )
	{
		messages[i].string = NULL;
		messages[i].synced = false;
	}
}

ovrMessageQueue::~ovrMessageQueue()
{
#if defined( OVR_BUILD_DEBUG )
	OVR_LOG( "%p:~ovrMessageQueue: destroying ... ", this );
#endif

	// Free any messages remaining on the queue.
	for ( ; ; )
	{
		const char * msg = GetNextMessage();
		if ( !msg ) {
			break;
		}
		OVR_LOG( "%p:~ovrMessageQueue: still on queue: %s", this, msg );
		free( (void *)msg );
	}

	// Free the queue itself.
	delete[] messages;

#if defined( OVR_BUILD_DEBUG )
	OVR_LOG( "%p:~ovrMessageQueue: destroying ... DONE", this );
#endif
}

void ovrMessageQueue::Shutdown()
{
	OVR_LOG( "%p:ovrMessageQueue shutdown", this );
	shutdown = true;

	if ( debug )
	{
		OVR_LOG( "%p:Shutdown() : notifying on processed", this );
	}
	processed.notify_all();

	if ( debug )
	{
		OVR_LOG( "%p:Shutdown() : notifying on posted", this );
	}
	posted.notify_all();
}


#if defined( OVR_OS_WIN32 ) && defined( PostMessage )
#undef PostMessage
#endif

// Thread safe, callable by any thread.
// The msg text is copied off before return, the caller can free
// the buffer.
// The app will abort() with a dump of all messages if the message
// buffer overflows.
bool ovrMessageQueue::PostMessage( const char * msg, bool sync, bool abortIfFull )
{
	if ( shutdown )
	{
		OVR_LOG( "%p:PostMessage( %s ) to shutdown queue", this, msg );
		return false;
	}
	if ( debug )
	{
		OVR_LOG( "%p:PostMessage( %s )", this, msg );
	}

	// mutex lock scope
	{
		std::unique_lock< std::mutex > lk( message_mutex );
		if ( tail - head >= maxMessages )
		{
			if ( abortIfFull )
			{
				OVR_LOG( "ovrMessageQueue overflow" );
				for ( int i = head; i < tail; i++ )
				{
					OVR_LOG( "%s", messages[i % maxMessages].string );
				}
				OVR_FAIL( "Message buffer overflowed" );
			}
			return false;
		}
		const int index = tail % maxMessages;
		messages[index].string = OVR_strdup( msg );
		messages[index].synced = sync;
		tail++;

		if ( debug )
		{
			OVR_LOG( "%p:PostMessage( '%s' ) : notifying on posted", this, msg );
		}

		posted.notify_all();

		if ( debug )
		{
			OVR_LOG( "%p:PostMessage( '%s' ) : sleep waiting on processed", this, msg );
		}

		// wait scope
		if ( sync )
		{
			processed.wait( lk );
		}
	}

	if ( debug )
	{
		OVR_LOG( "%p:PostMessage( '%s' ) : awoke after waiting on processed", this, msg );
	}

	return true;
}

void ovrMessageQueue::PostString( const char * msg )
{
	PostMessage( msg, false, true );
}

void ovrMessageQueue::PostPrintf( const char * fmt, ... )
{
	char bigBuffer[4096];
	va_list	args;
	va_start( args, fmt );
	vsnprintf( bigBuffer, sizeof( bigBuffer ), fmt, args );
	va_end( args );
	PostMessage( bigBuffer, false, true );
}

bool ovrMessageQueue::PostPrintfIfSpaceAvailable( const int requiredSpace, const char * fmt, ... )
{
	if ( SpaceAvailable() < requiredSpace )
	{
		return false;
	}
	char bigBuffer[4096];
	va_list	args;
	va_start( args, fmt );
	vsnprintf( bigBuffer, sizeof( bigBuffer ), fmt, args );
	va_end( args );
	PostMessage( bigBuffer, false, true );
	return true;
}

bool ovrMessageQueue::TryPostString( const char * msg )
{
	return PostMessage( msg, false, false );
}

bool ovrMessageQueue::TryPostPrintf( const char * fmt, ... )
{
	char bigBuffer[4096];
	va_list	args;
	va_start( args, fmt );
	vsnprintf( bigBuffer, sizeof( bigBuffer ), fmt, args );
	va_end( args );
	return PostMessage( bigBuffer, false, false );
}

void ovrMessageQueue::SendString( const char * msg )
{
	PostMessage( msg, true, true );
}

void ovrMessageQueue::SendPrintf( const char * fmt, ... )
{
	char bigBuffer[4096];
	va_list	args;
	va_start( args, fmt );
	vsnprintf( bigBuffer, sizeof( bigBuffer ), fmt, args );
	va_end( args );
	PostMessage( bigBuffer, true, true );
}

// Returns false if there are no more messages, otherwise returns
// a string that the caller must free.
const char * ovrMessageQueue::GetNextMessage()
{
	NotifyMessageProcessed();

	{
		std::unique_lock< std::mutex > lk( message_mutex );
		if ( tail > head )
		{
			const int index = head % maxMessages;
			const char * msg = messages[index].string;
			synced = messages[index].synced;
			messages[index].string = NULL;
			messages[index].synced = false;
			head++;

			if ( debug )
			{
				OVR_LOG( "%p:GetNextMessage() : %s", this, msg );
			}
			return msg;
		}
	}

	return nullptr;
}

// Returns immediately if there is already a message in the queue.
void ovrMessageQueue::SleepUntilMessage()
{
	NotifyMessageProcessed();

	// Guard
	{
		std::unique_lock< std::mutex > lk( message_mutex );
		if ( tail > head )
		{
			if ( debug )
			{
				OVR_LOG( "%p:SleepUntilMessage() : tail > head", this );
			}
			return;
		}

		if ( debug )
		{
			OVR_LOG( "%p:SleepUntilMessage() : sleep waiting on posted", this );
		}

		// Wait
		posted.wait( lk );
	}


	if ( debug )
	{
		OVR_LOG( "%p:SleepUntilMessage() : awoke after waiting on posted", this );
	}

}

void ovrMessageQueue::NotifyMessageProcessed()
{
	bool wasSynced = true;
	if ( synced.compare_exchange_strong( wasSynced, false ) )
	{
		if ( debug )
		{
			OVR_LOG( "%p:NotifyMessageProcessed() : notifying on processed", this );
		}
		processed.notify_all();
	}
}

void ovrMessageQueue::ClearMessages()
{
	if ( debug )
	{
		OVR_LOG( "%p:ClearMessages()", this );
	}
	for ( const char * msg = GetNextMessage(); msg != NULL; msg = GetNextMessage() )
	{
		OVR_LOG( "%p:ClearMessages: discarding %s", this, msg );
		free( (void *)msg );
	}
	if ( debug )
	{
		OVR_LOG( "%p:ClearMessages() COMPLETE", this );
	}
}

}	// namespace OVR
