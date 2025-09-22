/*
 File: ContFramePool.C
 
 Author:
 Date  : 
 
 */

/*--------------------------------------------------------------------------*/
/* 
 POSSIBLE IMPLEMENTATION
 -----------------------

 The class SimpleFramePool in file "simple_frame_pool.H/C" describes an
 incomplete vanilla implementation of a frame pool that allocates 
 *single* frames at a time. Because it does allocate one frame at a time, 
 it does not guarantee that a sequence of frames is allocated contiguously.
 This can cause problems.
 
 The class ContFramePool has the ability to allocate either single frames,
 or sequences of contiguous frames. This affects how we manage the
 free frames. In SimpleFramePool it is sufficient to maintain the free 
 frames.
 In ContFramePool we need to maintain free *sequences* of frames.
 
 This can be done in many ways, ranging from extensions to bitmaps to 
 free-lists of frames etc.
 
 IMPLEMENTATION:
 
 One simple way to manage sequences of free frames is to add a minor
 extension to the bitmap idea of SimpleFramePool: Instead of maintaining
 whether a frame is FREE or ALLOCATED, which requires one bit per frame, 
 we maintain whether the frame is FREE, or ALLOCATED, or HEAD-OF-SEQUENCE.
 The meaning of FREE is the same as in SimpleFramePool. 
 If a frame is marked as HEAD-OF-SEQUENCE, this means that it is allocated
 and that it is the first such frame in a sequence of frames. Allocated
 frames that are not first in a sequence are marked as ALLOCATED.
 
 NOTE: If we use this scheme to allocate only single frames, then all 
 frames are marked as either FREE or HEAD-OF-SEQUENCE.
 
 NOTE: In SimpleFramePool we needed only one bit to store the state of 
 each frame. Now we need two bits. In a first implementation you can choose
 to use one char per frame. This will allow you to check for a given status
 without having to do bit manipulations. Once you get this to work, 
 revisit the implementation and change it to using two bits. You will get 
 an efficiency penalty if you use one char (i.e., 8 bits) per frame when
 two bits do the trick.
 
 DETAILED IMPLEMENTATION:
 
 How can we use the HEAD-OF-SEQUENCE state to implement a contiguous
 allocator? Let's look a the individual functions:
 
 Constructor: Initialize all frames to FREE, except for any frames that you 
 need for the management of the frame pool, if any.
 
 get_frames(_n_frames): Traverse the "bitmap" of states and look for a 
 sequence of at least _n_frames entries that are FREE. If you find one, 
 mark the first one as HEAD-OF-SEQUENCE and the remaining _n_frames-1 as
 ALLOCATED.

 release_frames(_first_frame_no): Check whether the first frame is marked as
 HEAD-OF-SEQUENCE. If not, something went wrong. If it is, mark it as FREE.
 Traverse the subsequent frames until you reach one that is FREE or 
 HEAD-OF-SEQUENCE. Until then, mark the frames that you traverse as FREE.
 
 mark_inaccessible(_base_frame_no, _n_frames): This is no different than
 get_frames, without having to search for the free sequence. You tell the
 allocator exactly which frame to mark as HEAD-OF-SEQUENCE and how many
 frames after that to mark as ALLOCATED.
 
 needed_info_frames(_n_frames): This depends on how many bits you need 
 to store the state of each frame. If you use a char to represent the state
 of a frame, then you need one info frame for each FRAME_SIZE frames.
 
 A WORD ABOUT RELEASE_FRAMES():
 
 When we releae a frame, we only know its frame number. At the time
 of a frame's release, we don't know necessarily which pool it came
 from. Therefore, the function "release_frame" is static, i.e., 
 not associated with a particular frame pool.
 
 This problem is related to the lack of a so-called "placement delete" in
 C++. For a discussion of this see Stroustrup's FAQ:
 http://www.stroustrup.com/bs_faq2.html#placement-delete
 
 */
/*--------------------------------------------------------------------------*/


/*--------------------------------------------------------------------------*/
/* DEFINES */
/*--------------------------------------------------------------------------*/

/* -- (none) -- */

/*--------------------------------------------------------------------------*/
/* INCLUDES */
/*--------------------------------------------------------------------------*/

#include "cont_frame_pool.H"
#include "console.H"
#include "utils.H"
#include "assert.H"

/*--------------------------------------------------------------------------*/
/* DATA STRUCTURES */
/*--------------------------------------------------------------------------*/

// Static variables for tracking frame pools
ContFramePool* ContFramePool::frame_pools[Max_pools]; // Max 10 pools
unsigned int ContFramePool::num_pools = 0;

/*--------------------------------------------------------------------------*/
/* CONSTANTS */
/*--------------------------------------------------------------------------*/

/* -- (none) -- */

/*--------------------------------------------------------------------------*/
/* FORWARDS */
/*--------------------------------------------------------------------------*/

/* -- (none) -- */

/*--------------------------------------------------------------------------*/
/* HELPER FUNCTIONS */
/*--------------------------------------------------------------------------*/

ContFramePool::FrameState ContFramePool::get_state(unsigned long _frame_no) {
    // Convert frame number to bitmap index (2 bits per frame)
    unsigned int bitmap_index = _frame_no * 2 / 8;
    unsigned int bit_offset = (_frame_no * 2) % 8;
    
    unsigned char mask = 0x3 << bit_offset; // 2 bits mask
    unsigned char state_bits = (bitmap[bitmap_index] & mask) >> bit_offset;
    
    switch(state_bits) {
        case 0: return FrameState::Free;
        case 1: return FrameState::Used;
        case 2: return FrameState::HoS;
        case 3: return FrameState::Reserved;
        default: return FrameState::Free; // Should not happen
    }
}

void ContFramePool::set_state(unsigned long _frame_no, FrameState _state) {
    // Convert frame number to bitmap index (2 bits per frame)
    unsigned int bitmap_index = _frame_no * 2 / 8;
    unsigned int bit_offset = (_frame_no * 2) % 8;
    
    unsigned char mask = 0x3 << bit_offset; // 2 bits mask
    unsigned char state_bits;
    
    switch(_state) {
        case FrameState::Free:     state_bits = 0; break;
        case FrameState::Used:     state_bits = 1; break;
        case FrameState::HoS:      state_bits = 2; break;
        case FrameState::Reserved: state_bits = 3; break;
    }
    
    // Clear the bits first, then set them
    bitmap[bitmap_index] &= ~mask;
    bitmap[bitmap_index] |= (state_bits << bit_offset);
}

/*--------------------------------------------------------------------------*/
/* METHODS FOR CLASS   C o n t F r a m e P o o l */
/*--------------------------------------------------------------------------*/

ContFramePool::ContFramePool(unsigned long _base_frame_no,
                             unsigned long _n_frames,
                             unsigned long _info_frame_no)
{
    base_frame_no = _base_frame_no;
    nframes = _n_frames;
    nFreeFrames = _n_frames;
    info_frame_no = _info_frame_no;
    
    // Calculate bitmap size (2 bits per frame)
    unsigned long bitmap_size = (_n_frames * 2 + 7) / 8; // Round up to nearest byte
    
    // If _info_frame_no is zero, use first frame internally
    if (info_frame_no == 0) {
        bitmap = (unsigned char *) (base_frame_no * FRAME_SIZE);
    } else {
        bitmap = (unsigned char *) (info_frame_no * FRAME_SIZE);
    }
    
    // Initialize all frames as free
    for (unsigned long fno = 0; fno < _n_frames; fno++) {
        set_state(fno, FrameState::Free);
    }
    
    // If using internal management, mark first frame as used
    if (info_frame_no == 0) {
        set_state(0, FrameState::Used);
        nFreeFrames--;
    }
    
    // Add this pool to the static list
    if (num_pools < Max_pools) {
        frame_pools[num_pools] = this;
        num_pools++;
    }
    
    Console::puts("ContFramePool initialized\n");
}

unsigned long ContFramePool::get_frames(unsigned int _n_frames)
{
    // Check if we have enough free frames
    if (nFreeFrames < _n_frames) {
        return 0; // Not enough free frames
    }
    
    // Look for a contiguous sequence of free frames
    for (unsigned long start_frame = 0; start_frame <= nframes - _n_frames; start_frame++) {
        bool found_sequence = true;
        
        // Check if we can fit _n_frames starting at start_frame
        for (unsigned int i = 0; i < _n_frames; i++) {
            FrameState state = get_state(start_frame + i);
            if (state != FrameState::Free) {
                found_sequence = false;
                break;
            }
        }
        
        if (found_sequence) {
            // Mark first frame as Head-of-Sequence
            set_state(start_frame, FrameState::HoS);
            
            // Mark remaining frames as Used
            for (unsigned int i = 1; i < _n_frames; i++) {
                set_state(start_frame + i, FrameState::Used);
            }
            nFreeFrames -= _n_frames;
            
            return base_frame_no + start_frame;
        }
    }
    
    return 0; // No contiguous sequence found
}

void ContFramePool::mark_inaccessible(unsigned long _base_frame_no,
                                      unsigned long _n_frames)
{
    // Convert absolute frame number to relative frame number
    unsigned long relative_base = _base_frame_no - base_frame_no;
    
    // Check if the range is within this pool
    if (relative_base >= nframes || relative_base + _n_frames > nframes) {
        return; // Range is outside this pool
    }
    
    // Mark first frame as Head-of-Sequence
    set_state(relative_base, FrameState::HoS);
    
    // Mark remaining frames as Reserved (not Used, so they can't be accidentally released)
    for (unsigned long i = 1; i < _n_frames; i++) {
        set_state(relative_base + i, FrameState::Reserved);
    }
    nFreeFrames -= _n_frames;
}

void ContFramePool::release_frames(unsigned long _first_frame_no)
{
    // Find the pool that owns this frame
    for (unsigned int i = 0; i < num_pools; i++) {
        ContFramePool* pool = frame_pools[i];
        
        // Check if this frame belongs to this pool
        if (_first_frame_no >= pool->base_frame_no && 
            _first_frame_no < pool->base_frame_no + pool->nframes) {
            
            // Convert to relative frame number
            unsigned long relative_frame = _first_frame_no - pool->base_frame_no;
            
            // Check if this frame is marked as Head-of-Sequence
            if (pool->get_state(relative_frame) != FrameState::HoS) {
                Console::puts("ERROR: Trying to release frame that is not Head-of-Sequence!\n");
                assert(false);
                return;
            }
            
            // Release the sequence starting from this frame
            unsigned long current_frame = relative_frame;
            
            // First, mark the Head-of-Sequence frame as free
            pool->set_state(current_frame, FrameState::Free);
            pool->nFreeFrames++;
            current_frame++;
            
            // Now release all subsequent frames that are marked as Used
            // We need to be more careful here - only release frames that are part of this sequence
            while (current_frame < pool->nframes) {
                FrameState state = pool->get_state(current_frame);
                if (state == FrameState::Free) {
                    break; // End of sequence - we hit a free frame
                } else if (state == FrameState::HoS) {
                    break; // End of sequence - we hit another Head-of-Sequence
                } else if (state == FrameState::Reserved) {
                    break; // End of sequence - we hit a reserved frame (inaccessible)
                } else if (state == FrameState::Used) {
                    // This frame is part of our sequence, release it
                    pool->set_state(current_frame, FrameState::Free);
                    pool->nFreeFrames++;
                    current_frame++;
                } else {
                    // Unknown state, stop here
                    break;
                }
            }
            
            
            return;
        }
    }
    
    Console::puts("ERROR: Frame not found in any pool!\n");
    assert(false);
}

unsigned long ContFramePool::needed_info_frames(unsigned long _n_frames)
{
    // Calculate bitmap size needed (2 bits per frame)
    unsigned long bitmap_size_bytes = (_n_frames * 2 + 7) / 8; // Round up to nearest byte
    
    // Calculate how many frames are needed to store the bitmap
    unsigned long frames_needed = (bitmap_size_bytes + FRAME_SIZE - 1) / FRAME_SIZE; // Round up
    
    return frames_needed;
}
