/* Manual classic-rebase applier for the FreeBSD raw-clang dyld (the "dyld
 * rebuild" task).
 *
 * Why this exists: ld64.lld emits an EMPTY chained-starts payload
 * (seg_count 0), so rebaseDyld's chained path silently does nothing, and the
 * dyld3 opcode walker (forEachRebase) cannot be used here — it needs blocks,
 * Diagnostics/malloc and already-rebased pointers, none of which exist this
 * early. This function uses only pointer arithmetic and memory writes: no
 * libc calls, no blocks, no malloc, no throws.
 *
 * Contract: dyldMH is dyld's mach header at its SLID load address.
 * Returns true if classic rebase opcodes were found and applied (or if the
 * slide is 0 and there is nothing to do), false if anything looks off, in
 * which case the caller carries on unrebased.
 */
#include <stddef.h>
#include <mach-o/loader.h>
#include <stdint.h>
#include <stdbool.h>

#define MAX_SEGS 8

static uint64_t readUleb(const uint8_t** pp, const uint8_t* end)
{
    uint64_t result = 0;
    int bit = 0;
    while ( *pp < end ) {
        uint8_t b = **pp;
        (*pp)++;
        result |= ((uint64_t)(b & 0x7F)) << bit;
        bit += 7;
        if ( !(b & 0x80) )
            break;
    }
    return result;
}

bool rebaseDyldClassic(const void* dyldMH)
{
    const struct mach_header_64* mh = (const struct mach_header_64*)dyldMH;
    if ( mh->magic != MH_MAGIC_64 )
        return false;

    uint64_t segFileOff[MAX_SEGS];
    uint64_t segFileSize[MAX_SEGS];
    uint64_t segVM[MAX_SEGS];
    int nseg = 0;
    const uint8_t* rebaseOps = NULL;
    uint32_t rebaseSize = 0;

    const uint8_t* p = (const uint8_t*)(mh + 1);
    for ( uint32_t i = 0; i < mh->ncmds; ++i ) {
        const struct load_command* lc = (const struct load_command*)p;
        if ( lc->cmdsize < 8 )
            return false;
        if ( lc->cmd == LC_SEGMENT_64 ) {
            const struct segment_command_64* sg = (const struct segment_command_64*)p;
            if ( nseg < MAX_SEGS ) {
                segFileOff[nseg]  = sg->fileoff;
                segFileSize[nseg] = sg->filesize;
                segVM[nseg]       = sg->vmaddr;
                ++nseg;
            }
        }
        else if ( lc->cmd == LC_DYLD_INFO_ONLY ) {
            const struct dyld_info_command* dc = (const struct dyld_info_command*)p;
            /* file offset -> slid runtime address via owning segment */
            for ( int s = 0; s < nseg; ++s ) {
                if ( dc->rebase_off >= segFileOff[s] &&
                     dc->rebase_off < segFileOff[s] + segFileSize[s] ) {
                    /* actual segment base still unknown here (need slide
                     * first); remember file offset, resolve below */
                    rebaseOps  = (const uint8_t*)(uintptr_t)dc->rebase_off;
                    rebaseSize = dc->rebase_size;
                    break;
                }
            }
        }
        p += lc->cmdsize;
    }
    if ( rebaseOps == NULL || rebaseSize == 0 )
        return false;

    /* preferred base = vmaddr of the segment mapped from file offset 0 */
    uint64_t prefBase = 0;
    for ( int s = 0; s < nseg; ++s ) {
        if ( segFileOff[s] == 0 ) {
            prefBase = segVM[s];
            break;
        }
    }
    if ( prefBase == 0 )
        return false;
    uintptr_t slide = (uintptr_t)dyldMH - (uintptr_t)prefBase;
    if ( slide == 0 )
        return true;

    /* resolve opcode file offset to slid address */
    {
        uint64_t off = (uint64_t)(uintptr_t)rebaseOps;
        const uint8_t* ops = NULL;
        for ( int s = 0; s < nseg; ++s ) {
            if ( off >= segFileOff[s] && off < segFileOff[s] + segFileSize[s] ) {
                ops = (const uint8_t*)(segVM[s] + slide + (off - segFileOff[s]));
                break;
            }
        }
        if ( ops == NULL )
            return false;
        rebaseOps = ops;
    }

    const uint8_t* end = rebaseOps + rebaseSize;
    const uint8_t* q = rebaseOps;
    int segIndex = 0;
    uint64_t segOffset = 0;
    uint8_t type = 0;
    bool segSet = false;
    while ( q < end ) {
        uint8_t byte = *q++;
        uint8_t opcode = byte & 0xF0;
        uint8_t imm = byte & 0x0F;
        switch ( opcode ) {
            case 0x00: /* DONE */
                return true;
            case 0x10: /* SET_TYPE_IMM */
                if ( imm != 1 ) /* POINTER only; TEXT relocs need writable TEXT */
                    return false;
                type = imm;
                break;
            case 0x20: /* SET_SEGMENT_AND_OFFSET_ULEB */
                segIndex = imm;
                segOffset = readUleb(&q, end);
                segSet = true;
                break;
            case 0x30: /* ADD_ADDR_ULEB */
                segOffset += readUleb(&q, end);
                break;
            case 0x40: /* ADD_ADDR_IMM_SCALED */
                segOffset += (uint64_t)imm * 8;
                break;
            case 0x50: { /* DO_REBASE_IMM_TIMES */
                if ( !segSet || type != 1 )
                    return false;
                for ( int i = 0; i < imm; ++i ) {
                    uint64_t* loc = (uint64_t*)(segVM[segIndex] + slide + segOffset);
                    *loc += (uint64_t)slide;
                    segOffset += 8;
                }
                break;
            }
            case 0x60: { /* DO_REBASE_ULEB_TIMES */
                if ( !segSet || type != 1 )
                    return false;
                uint64_t count = readUleb(&q, end);
                for ( uint64_t i = 0; i < count; ++i ) {
                    uint64_t* loc = (uint64_t*)(segVM[segIndex] + slide + segOffset);
                    *loc += (uint64_t)slide;
                    segOffset += 8;
                }
                break;
            }
            case 0x70: { /* DO_REBASE_ADD_ADDR_ULEB */
                if ( !segSet || type != 1 )
                    return false;
                uint64_t* loc = (uint64_t*)(segVM[segIndex] + slide + segOffset);
                *loc += (uint64_t)slide;
                segOffset += readUleb(&q, end) + 8;
                break;
            }
            case 0x80: { /* DO_REBASE_ULEB_TIMES_SKIPPING_ULEB */
                if ( !segSet || type != 1 )
                    return false;
                uint64_t count = readUleb(&q, end);
                uint64_t skip = readUleb(&q, end);
                for ( uint64_t i = 0; i < count; ++i ) {
                    uint64_t* loc = (uint64_t*)(segVM[segIndex] + slide + segOffset);
                    *loc += (uint64_t)slide;
                    segOffset += skip + 8;
                }
                break;
            }
            default:
                return false;
        }
        if ( segIndex < 0 || segIndex >= nseg )
            return false;
    }
    return true;
}
