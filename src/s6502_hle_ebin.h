/* E.BIN high-level emulation blocks. Included inside s6502_exec(). */

#if defined(GAM4980_ENABLE_AGGRESSIVE_REGION_HLE) && !defined(GAM4980_NATIVE_GRAPHICS_ONLY)
  _hle_ebin_picture_head:
    {
      int hle_status = s6502_firmware_hle_picture_head_call(
          ac, iy, sp, status, cycles - executed,
          &s6502_hle_direct_result);

      if (hle_status <= 0) {
        if (hle_status < 0)
          S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_PICTURE_HEAD);
        else
          S6502_HLE_BUDGET_REJECT(S6502_HLE_ID_PICTURE_HEAD);
        goto _next;
      }
      CYCLES(s6502_hle_direct_result.cycles);
      S6502_HLE_RECORD(
          S6502_HLE_ID_PICTURE_HEAD,
          s6502_hle_direct_result.cycles);
      pc = s6502_hle_direct_result.pc;
      ac = s6502_hle_direct_result.ac;
      ix = s6502_hle_direct_result.ix;
      iy = s6502_hle_direct_result.iy;
      sp = s6502_hle_direct_result.dt;
      status = s6502_hle_direct_result.status;
      goto _exit;
    }

  _hle_ebin_picture_resume:
    {
      int hle_status = s6502_firmware_hle_picture_resume(
          ac, ix, iy, sp, status, cycles - executed,
          &s6502_hle_direct_result);

      if (hle_status <= 0) {
        if (hle_status < 0)
          S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_PICTURE_RESUME);
        else
          S6502_HLE_BUDGET_REJECT(S6502_HLE_ID_PICTURE_RESUME);
        goto _next;
      }
      CYCLES(s6502_hle_direct_result.cycles);
      S6502_HLE_RECORD(
          S6502_HLE_ID_PICTURE_RESUME,
          s6502_hle_direct_result.cycles);
      pc = s6502_hle_direct_result.pc;
      ac = s6502_hle_direct_result.ac;
      ix = s6502_hle_direct_result.ix;
      iy = s6502_hle_direct_result.iy;
      sp = s6502_hle_direct_result.dt;
      status = s6502_hle_direct_result.status;
      goto _exit;
    }

  _hle_ebin_picture_tail_next:
    dt = 1u;
    goto _hle_ebin_picture_tail;

  _hle_ebin_picture_tail_zero:
    dt = 0u;

  _hle_ebin_picture_tail:
    {
      s6502_hle_region_result_t hle_result;
      int hle_status;

      /* Fuse the common SysPicture row suffix: final masked byte, source
       * advance, $6646 LCD row-address update and the mode-3 row counter. */
      ea = (uint16_t)(s6502_stack_ram[0x3au] |
          ((uint16_t)s6502_stack_ram[0x3bu] << 8));
      hle_status = s6502_firmware_hle_picture_tail_call(
          dt != 0u, sp, status, cycles - executed, &hle_result);
      if (hle_status <= 0) {
        if (hle_status < 0)
          S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_PICTURE_TAIL);
        else
          S6502_HLE_BUDGET_REJECT(S6502_HLE_ID_PICTURE_TAIL);
        goto _next;
      }
      CYCLES(hle_result.cycles);
      S6502_HLE_RECORD(
          S6502_HLE_ID_PICTURE_TAIL, hle_result.cycles);
      pc = hle_result.pc;
      ac = hle_result.ac;
      ix = hle_result.ix;
      iy = hle_result.iy;
      sp = hle_result.sp;
      status = hle_result.status;
      if (dt == 0u)
        ea = 0x6c18u;
      dt = 0xffu;
      et = (uint16_t)(0x0100u + hle_result.ac);
      goto _exit;
    }

  _hle_ebin_shift_region:
    {
      s6502_hle_region_result_t hle_result;
      int hle_status;

      /* Merge the common mode-3 row beginning at $6988, including the
       * $6A75 inner loop, masked tail and shared $6646 row update. */
      hle_status = s6502_firmware_hle_shift_region_call(
          sp, status, cycles - executed, &hle_result);
      if (hle_status <= 0) {
        if (hle_status < 0)
          S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_SHIFT_REGION);
        else
          S6502_HLE_BUDGET_REJECT(S6502_HLE_ID_SHIFT_REGION);
        goto _next;
      }
      CYCLES(hle_result.cycles);
      S6502_HLE_RECORD_BATCH(
          S6502_HLE_ID_SHIFT_REGION, hle_result.rows, hle_result.cycles);
      pc = hle_result.pc;
      ac = hle_result.ac;
      ix = hle_result.ix;
      iy = hle_result.iy;
      sp = hle_result.sp;
      status = hle_result.status;
      goto _exit;
    }

  _hle_ebin_bitmap_region:
    {
      s6502_hle_region_result_t hle_result;
      int hle_status;

      /* Execute complete rows while they fit the current CPU slice.  Keeping
       * the $5C5D row boundary preserves the firmware timer/IRQ schedule but
       * still removes every inner AOT/interpreter dispatch in those rows. */
      hle_status = s6502_firmware_hle_bitmap_region_call(
          sp, status, cycles - executed, &hle_result);
      if (hle_status <= 0) {
        if (hle_status < 0)
          S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_BITMAP_REGION);
        else
          S6502_HLE_BUDGET_REJECT(S6502_HLE_ID_BITMAP_REGION);
        goto _next;
      }
      CYCLES(hle_result.cycles);
      S6502_HLE_RECORD_BATCH(
          S6502_HLE_ID_BITMAP_REGION, hle_result.rows, hle_result.cycles);
      pc = hle_result.pc;
      ac = hle_result.ac;
      ix = hle_result.ix;
      iy = hle_result.iy;
      sp = hle_result.sp;
      status = hle_result.status;
      goto _exit;
    }
#endif

#if defined(GAM4980_ENABLE_AGGRESSIVE_REGION_HLE) && !defined(GAM4980_NATIVE_GRAPHICS_ONLY)
  _hle_ebin_graphics_address:
    {
      int hle_status = s6502_firmware_hle_graphics_address(
          ix, iy, sp, status, cycles - executed,
          &s6502_hle_direct_result);

      if (hle_status <= 0) {
        S6502_HLE_BUDGET_REJECT(S6502_HLE_ID_GRAPHICS_ADDRESS);
        goto _next;
      }
      CYCLES(s6502_hle_direct_result.cycles);
      S6502_HLE_RECORD(
          S6502_HLE_ID_GRAPHICS_ADDRESS,
          s6502_hle_direct_result.cycles);
      pc = s6502_hle_direct_result.pc;
      ac = s6502_hle_direct_result.ac;
      ix = s6502_hle_direct_result.ix;
      iy = s6502_hle_direct_result.iy;
      sp = s6502_hle_direct_result.dt;
      status = s6502_hle_direct_result.status;
      goto _exit;
    }

  _hle_ebin_hline_middle:
    {
      int hle_status = s6502_firmware_hle_hline_middle(
          ac, ix, iy, sp, status, cycles - executed,
          &s6502_hle_direct_result);

      if (hle_status <= 0) {
        if (hle_status < 0)
          S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_HLINE_MIDDLE);
        else
          S6502_HLE_BUDGET_REJECT(S6502_HLE_ID_HLINE_MIDDLE);
        goto _next;
      }
      CYCLES(s6502_hle_direct_result.cycles);
      S6502_HLE_RECORD_BATCH(
          S6502_HLE_ID_HLINE_MIDDLE,
          s6502_hle_direct_result.hits,
          s6502_hle_direct_result.cycles);
      pc = s6502_hle_direct_result.pc;
      ea = s6502_hle_direct_result.ea;
      ac = s6502_hle_direct_result.ac;
      ix = s6502_hle_direct_result.ix;
      iy = s6502_hle_direct_result.iy;
      sp = s6502_hle_direct_result.dt;
      status = s6502_hle_direct_result.status;
      goto _exit;
    }

  _hle_ebin_part_picture_row_right:
    dt = 0u;
    goto _hle_ebin_part_picture_row;

  _hle_ebin_part_picture_row_left:
    dt = 1u;

  _hle_ebin_part_picture_row:
    {
      int hle_status = s6502_firmware_hle_part_picture_row(
          dt != 0u, ac, iy, sp, status, cycles - executed,
          &s6502_hle_direct_result);

      if (hle_status <= 0) {
        if (hle_status < 0)
          S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_PART_PICTURE_ROW);
        else
          S6502_HLE_BUDGET_REJECT(S6502_HLE_ID_PART_PICTURE_ROW);
        goto _next;
      }
      CYCLES(s6502_hle_direct_result.cycles);
      S6502_HLE_RECORD_BATCH(
          S6502_HLE_ID_PART_PICTURE_ROW,
          s6502_hle_direct_result.hits,
          s6502_hle_direct_result.cycles);
      pc = s6502_hle_direct_result.pc;
      ea = s6502_hle_direct_result.ea;
      ac = s6502_hle_direct_result.ac;
      ix = s6502_hle_direct_result.ix;
      iy = s6502_hle_direct_result.iy;
      sp = s6502_hle_direct_result.dt;
      status = s6502_hle_direct_result.status;
      goto _exit;
    }

  _hle_ebin_pixel_tail:
    {
      int hle_status = s6502_firmware_hle_pixel_tail(
          sp, status, cycles - executed, &s6502_hle_direct_result);

      if (hle_status <= 0) {
        S6502_HLE_BUDGET_REJECT(S6502_HLE_ID_PIXEL_TAIL);
        goto _next;
      }
      CYCLES(s6502_hle_direct_result.cycles);
      S6502_HLE_RECORD(
          S6502_HLE_ID_PIXEL_TAIL,
          s6502_hle_direct_result.cycles);
      pc = s6502_hle_direct_result.pc;
      ea = s6502_hle_direct_result.ea;
      ac = s6502_hle_direct_result.ac;
      ix = s6502_hle_direct_result.ix;
      iy = s6502_hle_direct_result.iy;
      sp = s6502_hle_direct_result.dt;
      status = s6502_hle_direct_result.status;
      goto _exit;
    }
#endif

  _hle_ebin_bitmap_copy:
    {
      uint8_t *hle_ram = s6502_stack_ram;
      uint32_t hle_batch_cycles = 0u;
      uint32_t hle_batch_hits = 0u;
      uint8_t hle_shift;
      uint8_t hle_dest_high;
      uint8_t hle_dest_carry;

      /* Execute as many complete $5CB3-$5D04 iterations as fit this CPU
       * slice.  IRQ/timer checks occur between s6502_exec() calls, so this
       * removes only redundant AOT dispatches inside the same slice. */
#ifdef GAM4980_ENABLE_DIRECT_RAM_HLE
      {
        if (s6502_hle_try_direct_bitmap_copy(
                hle_ram, dt, cycles - executed, et, status)) {
          CYCLES(s6502_hle_direct_result.cycles);
          hle_batch_cycles = s6502_hle_direct_result.cycles;
          hle_batch_hits = s6502_hle_direct_result.hits;
          pc = s6502_hle_direct_result.pc;
          ea = s6502_hle_direct_result.ea;
          ac = s6502_hle_direct_result.ac;
          ix = s6502_hle_direct_result.ix;
          iy = s6502_hle_direct_result.iy;
          dt = s6502_hle_direct_result.dt;
          status = s6502_hle_direct_result.status;
          if (hle_batch_hits > 1u)
            S6502_HLE_ATTEMPT_EXTRA(
                S6502_HLE_ID_BITMAP_COPY, hle_batch_hits - 1u);
          S6502_HLE_RECORD_BATCH(
              S6502_HLE_ID_BITMAP_COPY, hle_batch_hits, hle_batch_cycles);
          S6502_HLE_RECORD_DIRECT_BATCH(
              S6502_HLE_ID_BITMAP_COPY, hle_batch_hits);
          goto _exit;
        }
      }
#endif

      for (;;) {
        CYCLES(et);
        hle_batch_cycles += et;
        ++hle_batch_hits;

        iy = 0u;
        ea = (uint16_t)(hle_ram[0x2fu] | (hle_ram[0x30u] << 8));
        ac = READ8(ea);
        WRITE8(0x20e5u, ac);
        ea = (uint16_t)(ea + 1u);
        hle_ram[0x2fu] = (uint8_t)ea;
        hle_ram[0x30u] = (uint8_t)(ea >> 8);
        ix = READ8(ea);
        WRITE8(0x20e6u, ix);

        hle_shift = READ8(0x20cfu);
        et = (uint16_t)(((uint16_t)ac << 8) | ix);
        et = (uint16_t)(et >> hle_shift);
        WRITE8(0x20e5u, (uint8_t)(et >> 8));
        WRITE8(0x20e6u, (uint8_t)et);
        ix = 0u;

        ea = (uint16_t)(hle_ram[0x31u] | (hle_ram[0x32u] << 8));
        ac = (uint8_t)et;
        WRITE8(ea, ac);
        hle_dest_high = (uint8_t)(ea >> 8);
        hle_dest_carry = (uint8_t)((uint8_t)ea == 0xffu);
        ea = (uint16_t)(ea + 1u);
        hle_ram[0x31u] = (uint8_t)ea;
        hle_ram[0x32u] = (uint8_t)(ea >> 8);
        SET_V(hle_dest_carry && hle_dest_high == 0x7fu);

        dt = (uint8_t)(READ8(0x20d8u) - 1u);
        WRITE8(0x20d8u, dt);
        ac = dt;
        SET_C(1);
        SET_NZ(ac);
        pc = ac ? 0x5cb3u : 0x5d05u;
        if (!ac || executed >= cycles || sys_halt_p())
          break;
        hle_shift = READ8(0x20cfu);
        if (!(GAM4980_FIRMWARE_HLE_MASK & S6502_HLE_BITMAP_COPY) ||
            !s6502_firmware_hle_enabled || DECIMAL_p ||
            s6502_firmware_hle_banks[5] != 0x0eb8u ||
            s6502_firmware_hle_bitmap_validation != 1u ||
            hle_shift > 7u)
          break;
        dt = (uint8_t)(READ8(0x20d8u) - 1u);
        et = (uint16_t)((hle_shift == 0u ? 55u :
            (uint16_t)(53u + 19u * hle_shift)) +
            (dt ? 48u : 46u));
        if ((uint32_t)et > cycles - executed)
          break;
      }
      if (hle_batch_hits > 1u)
        S6502_HLE_ATTEMPT_EXTRA(
            S6502_HLE_ID_BITMAP_COPY, hle_batch_hits - 1u);
      S6502_HLE_RECORD_BATCH(
          S6502_HLE_ID_BITMAP_COPY, hle_batch_hits, hle_batch_cycles);
      goto _exit;
    }

  _hle_ebin_bitmap_copy_suffix_5ce5:
    {
      uint8_t *hle_ram = s6502_stack_ram;
      uint8_t hle_dest_high;
      uint8_t hle_dest_carry;

      /* Resume after the variable-count $20E5:$20E6 shift loop. */
      CYCLES(et);
      S6502_HLE_RECORD(S6502_HLE_ID_BITMAP_COPY, et);
      iy = 0u;
      ea = (uint16_t)(hle_ram[0x31u] | (hle_ram[0x32u] << 8));
      ac = READ8(0x20e6u);
      WRITE8(ea, ac);
      hle_dest_high = (uint8_t)(ea >> 8);
      hle_dest_carry = (uint8_t)((uint8_t)ea == 0xffu);
      ea = (uint16_t)(ea + 1u);
      hle_ram[0x31u] = (uint8_t)ea;
      hle_ram[0x32u] = (uint8_t)(ea >> 8);
      SET_V(hle_dest_carry && hle_dest_high == 0x7fu);
      WRITE8(0x20d8u, dt);
      ac = READ8(0x20d8u);
      SET_C(1);
      SET_NZ(ac);
      pc = ac ? 0x5cb3u : 0x5d05u;
      goto _exit;
    }

#ifndef GAM4980_NATIVE_GRAPHICS_ONLY
  _hle_ebin_glyph_row:
    {
      s6502_hle_glyph_result_t hle_result;
      uint32_t hle_batch_cycles = 0u;
      uint32_t hle_batch_hits = 0u;

      /* Batch complete ASCII rows while preserving the original $650B CPX /
       * BEQ boundary and never crossing the scheduler's cycle budget. */
      for (;;) {
        CYCLES(et);
        hle_batch_cycles += et;
        ++hle_batch_hits;
        s6502_firmware_hle_glyph_row(ix, sp, status, &hle_result);
        ac = hle_result.ac;
        ix = hle_result.ix;
        iy = hle_result.iy;
        status = hle_result.status;
        if (ix >= 0x10u || executed >= cycles || sys_halt_p())
          break;
        et = s6502_firmware_hle_glyph_row_cycles(ix, 0);
        if ((uint32_t)et + 4u > cycles - executed)
          break;
        /* CPX #$10; BEQ (not taken). */
        status = (uint8_t)((status & ~0x83u) |
            ((uint8_t)(ix - 0x10u) & 0x80u));
        CYCLES(4u);
        hle_batch_cycles += 4u;
      }
      if (hle_batch_hits > 1u)
        S6502_HLE_ATTEMPT_EXTRA(
            S6502_HLE_ID_GLYPH_ROW, hle_batch_hits - 1u);
      S6502_HLE_RECORD_BATCH(
          S6502_HLE_ID_GLYPH_ROW, hle_batch_hits, hle_batch_cycles);
      pc = 0x650bu;
      goto _exit;
    }

  _hle_ebin_wide_glyph_row:
    {
      s6502_hle_glyph_result_t hle_result;
      uint32_t hle_batch_cycles = 0u;
      uint32_t hle_batch_hits = 0u;

      /* The Chinese compositor consumes two source bytes per row, hence X
       * advances by two and the loop terminates at $20. */
      for (;;) {
        CYCLES(et);
        hle_batch_cycles += et;
        ++hle_batch_hits;
        s6502_firmware_hle_wide_glyph_row(ix, sp, status, &hle_result);
        ac = hle_result.ac;
        ix = hle_result.ix;
        iy = hle_result.iy;
        status = hle_result.status;
        if (ix >= 0x20u || executed >= cycles || sys_halt_p())
          break;
        et = s6502_firmware_hle_glyph_row_cycles(ix, 1);
        if ((uint32_t)et + 4u > cycles - executed)
          break;
        /* CPX #$20; BEQ (not taken). */
        status = (uint8_t)((status & ~0x83u) |
            ((uint8_t)(ix - 0x20u) & 0x80u));
        CYCLES(4u);
        hle_batch_cycles += 4u;
      }
      if (hle_batch_hits > 1u)
        S6502_HLE_ATTEMPT_EXTRA(
            S6502_HLE_ID_WIDE_GLYPH, hle_batch_hits - 1u);
      S6502_HLE_RECORD_BATCH(
          S6502_HLE_ID_WIDE_GLYPH, hle_batch_hits, hle_batch_cycles);
      pc = 0x6086u;
      goto _exit;
    }

#endif
  _hle_ebin_shift_blit:
    {
      uint8_t *hle_ram = s6502_stack_ram;
      uint32_t hle_batch_cycles = 0u;
      uint32_t hle_batch_hits = 0u;
      uint8_t hle_shift;

      /* Batch complete $6A75-$6B04 iterations inside the current CPU slice;
       * stop before the first iteration that would cross its cycle budget. */
#if defined(GAM4980_ENABLE_DIRECT_RAM_HLE) || \
    defined(GAM4980_ENABLE_AGGRESSIVE_REGION_HLE)
      {
        if (s6502_hle_try_direct_shift_blit(
                hle_ram, dt, cycles - executed, et, status)) {
          CYCLES(s6502_hle_direct_result.cycles);
          hle_batch_cycles = s6502_hle_direct_result.cycles;
          hle_batch_hits = s6502_hle_direct_result.hits;
          pc = s6502_hle_direct_result.pc;
          ea = s6502_hle_direct_result.ea;
          ac = s6502_hle_direct_result.ac;
          ix = s6502_hle_direct_result.ix;
          iy = s6502_hle_direct_result.iy;
          dt = s6502_hle_direct_result.dt;
          status = s6502_hle_direct_result.status;
          if (hle_batch_hits > 1u)
            S6502_HLE_ATTEMPT_EXTRA(
                S6502_HLE_ID_SHIFT_BLIT, hle_batch_hits - 1u);
          S6502_HLE_RECORD_BATCH(
              S6502_HLE_ID_SHIFT_BLIT, hle_batch_hits, hle_batch_cycles);
          S6502_HLE_RECORD_DIRECT_BATCH(
              S6502_HLE_ID_SHIFT_BLIT, hle_batch_hits);
          goto _exit;
        }
      }
#endif

      for (;;) {
        CYCLES(et);
        hle_batch_cycles += et;
        ++hle_batch_hits;
        iy = READ8(0x20cfu);
        et = (uint16_t)(hle_ram[0x2fu] | (hle_ram[0x30u] << 8));
        ac = READ8(et);
        WRITE8(0x20e5u, ac);
        et = (uint16_t)(et + 1u);
        hle_ram[0x2fu] = (uint8_t)et;
        hle_ram[0x30u] = (uint8_t)(et >> 8);
        ix = READ8(et);
        WRITE8(0x20e6u, ix);
        et = (uint16_t)(((uint16_t)ac << 8) | ix);
        et = (uint16_t)(et >> iy);
        WRITE8(0x20e5u, (uint8_t)(et >> 8));
        WRITE8(0x20e6u, (uint8_t)et);
        ix = 0u;
        iy = 0u;

        if (ea == 0x0400u) {
          ac = READ8(0x20e6u);
          WRITE8(0x1000u, ac);
          ea = 0x0400u;
        } else {
          ac = READ8(0x20e6u);
          WRITE8(ea, ac);
        }
        ea = (uint16_t)(ea + 1u);
        hle_ram[0x3au] = (uint8_t)ea;
        hle_ram[0x3bu] = (uint8_t)(ea >> 8);

        WRITE8(0x20d8u, dt);
        ac = READ8(0x20d8u);
        SET_C(1);
        SET_NZ(ac);
        pc = ac ? 0x6a75u : 0x6b05u;
        if (!ac || executed >= cycles || sys_halt_p())
          break;
        hle_shift = READ8(0x20cfu);
        if (!(GAM4980_FIRMWARE_HLE_MASK & S6502_HLE_SHIFT_BLIT) ||
            !s6502_firmware_hle_enabled || DECIMAL_p ||
            s6502_firmware_hle_banks[6] != 0x0eb5u ||
            s6502_firmware_hle_validation != 1u || hle_shift > 7u)
          break;
        ea = (uint16_t)(hle_ram[0x3au] | (hle_ram[0x3bu] << 8));
        dt = (uint8_t)(READ8(0x20d8u) - 1u);
        et = (uint16_t)((hle_shift == 0u ? 55u :
            (uint16_t)(53u + 19u * hle_shift)) +
            (ea == 0x0400u ? 75u :
                ((ea >> 8) != 0x04u ? 31u : 41u)) +
            (dt ? 39u : 37u));
        if ((uint32_t)et > cycles - executed)
          break;
      }
      if (hle_batch_hits > 1u)
        S6502_HLE_ATTEMPT_EXTRA(
            S6502_HLE_ID_SHIFT_BLIT, hle_batch_hits - 1u);
      S6502_HLE_RECORD_BATCH(
          S6502_HLE_ID_SHIFT_BLIT, hle_batch_hits, hle_batch_cycles);
      goto _exit;
    }

  _hle_ebin_shift_blit_suffix_6aa7:
    {
      uint8_t *hle_ram = s6502_stack_ram;

      /* Resume exactly after the variable-count shift loop.  This entry is
       * reached when the full $6A75 HLE did not fit the previous exec slice;
       * its short suffix now fits without borrowing cycles from the next
       * timer/interrupt boundary. */
      CYCLES(et);
      S6502_HLE_RECORD(S6502_HLE_ID_SHIFT_BLIT, et);
      if (ea == 0x0400u) {
        ac = READ8(0x20e6u);
        WRITE8(0x1000u, ac);
        ea = 0x0400u;
      } else {
        ac = READ8(0x20e6u);
        WRITE8(ea, ac);
      }
      ea = (uint16_t)(ea + 1u);
      hle_ram[0x3au] = (uint8_t)ea;
      hle_ram[0x3bu] = (uint8_t)(ea >> 8);
      WRITE8(0x20d8u, dt);
      ac = READ8(0x20d8u);
      SET_C(1);
      SET_NZ(ac);
      pc = ac ? 0x6a75u : 0x6b05u;
      goto _exit;
    }

  _hle_ebin_shift_blit_suffix_6ae0:
    {
      uint8_t *hle_ram = s6502_stack_ram;

      /* Common non-aliased destination path, after the $03E5-$03E7 checks. */
      CYCLES(et);
      S6502_HLE_RECORD(S6502_HLE_ID_SHIFT_BLIT, et);
      iy = 0u;
      ea = (uint16_t)(hle_ram[0x3au] | (hle_ram[0x3bu] << 8));
      ac = READ8(0x20e6u);
      WRITE8(ea, ac);
      ea = (uint16_t)(ea + 1u);
      hle_ram[0x3au] = (uint8_t)ea;
      hle_ram[0x3bu] = (uint8_t)(ea >> 8);
      WRITE8(0x20d8u, dt);
      ac = READ8(0x20d8u);
      SET_C(1);
      SET_NZ(ac);
      pc = ac ? 0x6a75u : 0x6b05u;
      goto _exit;
    }

  _hle_ebin_byte_fill:
    ea = ix;
    dt = 0u;
    goto _hle_ebin_byte_transfer;

  _hle_ebin_byte_fill_partial:
    dt = 1u;

  _hle_ebin_byte_transfer:
    {
      uint16_t count = ea;
      uint8_t value = dt ? (uint8_t)ac : 0u;
      uint8_t x = (uint8_t)ix, y = (uint8_t)iy;
      CYCLES(et);
      S6502_HLE_RECORD(S6502_HLE_ID_BYTE_FILL, et);
      /* DATA3 is an auto-incrementing port, not a constant fill byte.
       * Full and partial entries share this single ordered transfer kernel. */
      while (count--) {
        uint16_t address;
        value = READ8(0x0003u);
        address = (uint16_t)(s6502_stack_ram[0x2fu] |
            ((uint16_t)s6502_stack_ram[0x30u] << 8));
        WRITE8((uint16_t)(address + y), value);
        ++y; --x;
      }
      ac = value; ix = x; iy = y;
      SET_C(1);
      SET_NZ(ix);
      pc = dt ? 0x7937u : 0x7940u;
      goto _exit;
    }

  _hle_ebin_bank_get:
    {
      uint16_t pointer;
      CYCLES(et);
      S6502_HLE_RECORD(S6502_HLE_ID_BANK_SWITCH, et);
      PUSH(status | FLAG_B | FLAG_U);
      PUSH(ac);
      pointer = READ16(0x28u);
      WRITE8(0x2fu, READ8(pointer));
      WRITE8(0x30u, READ8((uint16_t)(pointer + 1u)));
      ac = POP();
      WRITE8(0x0cu, ac);
      ac = READ8(0x0du);
      WRITE8(READ16(0x2fu), ac);
      ac = READ8(0x0eu);
      iy = 1u;
      WRITE8((uint16_t)(READ16(0x2fu) + 1u), ac);
      status = POP() | FLAG_U | FLAG_B;
      pc = POP(); pc |= (uint16_t)POP() << 8; ++pc;
      goto _exit;
    }

  _hle_ebin_bank_range:
    {
      uint16_t pointer, sum;
      CYCLES(et);
      S6502_HLE_RECORD(S6502_HLE_ID_BANK_SWITCH, et);
      if(pc==0xf475u) {
        PUSH(status | FLAG_B | FLAG_U);
        WRITE8(0x0cu,ac);
        pointer=(uint16_t)(READ8(0x28u)|((uint16_t)READ8(0x29u)<<8));
        ac=READ8((uint16_t)(pointer+1u)); WRITE8(0x0du,ac);
        ac=READ8((uint16_t)(pointer+2u)); WRITE8(0x0eu,ac);
        PUSH(ac);
        iy=0;
        ix=(uint8_t)(READ8(pointer)-1u);
      }
      while(ix) {
        sum=(uint16_t)(READ8(0x0du)+1u);
        WRITE8(0x0cu,(uint8_t)(READ8(0x0cu)+1u));
        WRITE8(0x0du,(uint8_t)sum);
        ac=(uint8_t)(POP()+(sum>>8));
        WRITE8(0x0eu,ac);
        PUSH(ac);
        --ix;
      }
      ac=POP();
      status=(uint8_t)(POP()|FLAG_U|FLAG_B);
      pc=POP(); pc=(uint16_t)(pc|((uint16_t)POP()<<8)); ++pc;
      goto _exit;
    }

  _hle_ebin_bank_query:
    {
      uint16_t bank, base, difference;
      uint8_t bias;
      CYCLES(et);
      ++s6502_bank_query_hits;
      s6502_bank_query_cycles += et;
      S6502_HLE_RECORD(S6502_HLE_ID_BANK_SWITCH, et);
      if (pc == 0xf4a5u) {
        PUSH(status | FLAG_B | FLAG_U);
        PUSH(ix);
        PUSH(iy);
      }
      if (pc != 0xf4afu) WRITE8(0x000cu, 5u);
      bank = (uint16_t)(READ8(0x000du) | ((uint16_t)READ8(0x000eu) << 8));
      bias = (bank >> 8) < READ8(0x03d5u) ? 0xe0u : 0u;
      WRITE8(0x2000u, bias);
      base = bias ? (uint16_t)(READ8(0x2029u) | ((uint16_t)READ8(0x202au) << 8))
                  : (uint16_t)(READ8(0x03d6u) | ((uint16_t)READ8(0x03d5u) << 8));
      difference = (uint16_t)(bank - base);
      WRITE8(0x2000u, (uint8_t)((difference >> 2) + bias));
      iy = POP();
      ix = POP();
      ac = READ8(0x2000u);
      status = (uint8_t)(POP() | FLAG_U | FLAG_B);
      pc = POP();
      pc = (uint16_t)(pc | ((uint16_t)POP() << 8));
      pc = (uint16_t)(pc + 1u);
      goto _exit;
    }

  _hle_ebin_bank_switch:
    {
      /* E.BIN $F52A-$F5BC: fgf_switch_bank_number().  This is the
       * compiler/runtime bank trampoline shared by A-series GAM programs.
       * Keep its real stack frame and low-memory register traffic: both can
       * be observed by callers even though the arithmetic itself is simple. */
      CYCLES(et);
      S6502_HLE_RECORD(S6502_HLE_ID_BANK_SWITCH, et);
      PUSH(status | FLAG_B | FLAG_U);
      SET_I(1);
      WRITE8(0x2000u, ac);
      ac = ix;
      SET_NZ(ac);
      PUSH(ac);
      ac = iy;
      SET_NZ(ac);
      PUSH(ac);
      ac = READ8(0x2000u);
      SET_NZ(ac);
      if (ac >= 0xe0u) {
        SET_C(1);
        dt = 0xe0u;
        et = (uint16_t)(ac - dt);
        SET_C(ac >= dt);
        SET_V((ac ^ dt) & (ac ^ et) & 0x80u);
        ac = (uint8_t)et;
        SET_NZ(ac);
        WRITE8(0x2000u, ac);
        ac = READ8(0x2029u);
        SET_NZ(ac);
        WRITE8(0x2001u, ac);
        ac = READ8(0x202au);
        SET_NZ(ac);
        WRITE8(0x2002u, ac);
      } else {
        ac = READ8(0x03d6u);
        SET_NZ(ac);
        WRITE8(0x2001u, ac);
        ac = READ8(0x03d5u);
        SET_NZ(ac);
        WRITE8(0x2002u, ac);
      }
      goto _hle_ebin_bank_switch_do;
    }

  _hle_ebin_bank_switch_f549:
    {
      /* Suffix entry after the prologue and A >= $E0 branch. */
      CYCLES(et);
      S6502_HLE_RECORD(S6502_HLE_ID_BANK_SWITCH, et);
      SET_C(1);
      dt = 0xe0u;
      et = (uint16_t)(ac - dt);
      SET_C(ac >= dt);
      SET_V((ac ^ dt) & (ac ^ et) & 0x80u);
      ac = (uint8_t)et;
      SET_NZ(ac);
      WRITE8(0x2000u, ac);
      ac = READ8(0x2029u);
      SET_NZ(ac);
      WRITE8(0x2001u, ac);
      ac = READ8(0x202au);
      SET_NZ(ac);
      WRITE8(0x2002u, ac);
      goto _hle_ebin_bank_switch_do;
    }

  _hle_ebin_bank_switch_f55b:
    {
      /* Suffix entry with BankSwitchTemp/Temp1 already prepared. */
      CYCLES(et);
      S6502_HLE_RECORD(S6502_HLE_ID_BANK_SWITCH, et);
      goto _hle_ebin_bank_switch_do;
    }

  _hle_ebin_bank_switch_do:
    {
      uint16_t hle_sum;
      uint8_t hle_bank;

      /* Mapping is a native 16-bit offset calculation. The caller's P is
       * restored below, so none of the intermediate arithmetic flags live. */
      WRITE8(0x000cu, 5u);
      hle_sum = (uint16_t)((uint16_t)READ8(0x2000u) << 2);
      iy = hle_sum >> 8;
      hle_sum = (uint16_t)((hle_sum & 255u) + READ8(0x2001u));
      WRITE8(0x000du, (uint8_t)hle_sum);
      ac = (uint8_t)(iy + READ8(0x2002u) + (hle_sum >> 8));
      WRITE8(0x000eu, ac);
      WRITE8(0x2000u, ac);
      for (hle_bank = 0u; hle_bank < 3u; ++hle_bank) {
        hle_sum = (uint16_t)(READ8(0x000du) + 1u);
        dt = (uint8_t)(READ8(0x000cu) + 1u);
        WRITE8(0x000cu, dt);
        WRITE8(0x000du, (uint8_t)hle_sum);
        ac = (uint8_t)(READ8(0x2000u) + (hle_sum >> 8));
        WRITE8(0x000eu, ac);
        WRITE8(0x2000u, ac);
      }

      ac = POP();
      SET_NZ(ac);
      iy = ac;
      SET_NZ(iy);
      ac = POP();
      SET_NZ(ac);
      ix = ac;
      SET_NZ(ix);
      status = (uint8_t)(POP() | FLAG_U | FLAG_B);
      pc = POP();
      pc = (uint16_t)(pc | ((uint16_t)POP() << 8));
      pc = (uint16_t)(pc + 1u);
      goto _exit;
    }

  _hle_ebin_and_long:
    {
      uint16_t left = READ16W(0x20u), right = READ16W(0x23u);
      uint16_t output = READ16W(0x2au);
      unsigned i;
      CYCLES(et);
      S6502_HLE_RECORD(S6502_HLE_ID_AND_LONG, et);
      /* Keep sequential memory effects, not per-byte virtual flags. */
      for (i = 0u; i < 4u; ++i) {
        ac = READ8((uint16_t)(left + i));
        dt = READ8((uint16_t)(right + i));
        ac &= dt;
        WRITE8((uint16_t)(output + 8u + i), ac);
      }
      iy = 11u;
      ea = 1u;
      goto _hle_ebin_temp_return;
    }

  _hle_ebin_load_oper1_temp:
    CYCLES(et);
    S6502_HLE_RECORD(S6502_HLE_ID_LOAD_OPER1_TEMP, et);
    ea = 0u;

  _hle_ebin_temp_return:
    {
      uint16_t base = READ16W(0x2au);
      uint16_t address = (uint16_t)(base + 8u);
      /* Shared native address/return adapter for D2CA and D596. */
      if (ea) {
        s6502_stack_ram[0x100u | (uint8_t)sp] = 0xd2u;
        s6502_stack_ram[0x100u | (uint8_t)(sp - 1u)] = 0xf4u;
        s6502_stack_ram[0x100u | (uint8_t)(sp - 2u)] = (uint8_t)ac;
      } else {
        s6502_stack_ram[0x100u | (uint8_t)sp] = (uint8_t)ac;
      }
      WRITE8(0x20u, (uint8_t)address);
      WRITE8(0x21u, (uint8_t)(address >> 8));
      status = (fw_add16_status(base, 8u, status) & ~0x82u) |
          (ac & 128u) | (ac ? 0u : 2u);
      pc = s6502_stack_ram[0x100u | (uint8_t)(sp + 1u)] |
          ((uint16_t)s6502_stack_ram[0x100u | (uint8_t)(sp + 2u)] << 8);
      sp = (uint8_t)(sp + 2u);
      pc = (uint16_t)(pc + 1u);
      dt = 0u;
      goto _exit;
    }

  _hle_ebin_indirect_call:
    {
      uint16_t target = READ16W(0x26u);
      uint16_t minus = (uint16_t)(target - 1u);
      CYCLES(et);
      S6502_HLE_RECORD(S6502_HLE_ID_INDIRECT_CALL, et);
      WRITE8(0x26u, (uint8_t)minus);
      WRITE8(0x27u, (uint8_t)(minus >> 8));
      s6502_stack_ram[0x100u | (uint8_t)sp] = (uint8_t)(minus >> 8);
      s6502_stack_ram[0x100u | (uint8_t)(sp - 1u)] = (uint8_t)minus;
      iy = ac;
      status = (fw_sub16_status(target, 1u, status) & ~0x82u) |
          (ac & 128u) | (ac ? 0u : 2u);
      pc = target;
      goto _exit;
    }

  _hle_ebin_multiply16:
    {
      uint8_t *ram = s6502_stack_ram;
      uint32_t a = ram[0x20u] | ((uint32_t)ram[0x21u] << 8);
      uint32_t b = ram[0x23u] | ((uint32_t)ram[0x24u] << 8);
      uint16_t product = (uint16_t)(a * b);
      CYCLES(et);
      S6502_HLE_RECORD(S6502_HLE_ID_MULTIPLY16, et);
      ram[0x100u | (uint8_t)sp] = (uint8_t)(b >> 8);
      ram[0x100u | (uint8_t)(sp - 1u)] = (uint8_t)b;
      status = fw_mul16_status(a, b, status);
      if (a && b) ix = 0u;
      ram[0x20u] = ram[0x26u] = (uint8_t)product;
      ram[0x21u] = ram[0x27u] = (uint8_t)(product >> 8);
      ac = b >> 8;
      SET_NZ(ac);
      pc = ram[0x100u | (uint8_t)(sp + 1u)] |
          ((uint16_t)ram[0x100u | (uint8_t)(sp + 2u)] << 8);
      sp = (uint8_t)(sp + 2u);
      pc = (uint16_t)(pc + 1u);
      goto _exit;
    }

  _hle_ebin_compare_long:
    {
      uint16_t hle_left = READ16W(0x0020u);
      uint16_t hle_right = READ16W(0x0023u);
      uint32_t hle_a = 0u, hle_b = 0u;
      uint8_t hle_index;
      uint8_t hle_nonzero = 0u;
      uint8_t hle_final_status;

      /* E.BIN $D362-$D39A: __cmp_long().  This is the four-byte analogue
       * of the existing __cmp_int HLE and is emitted by the same C compiler
       * in all three sampled games. */
      CYCLES(et);
      S6502_HLE_RECORD(S6502_HLE_ID_COMPARE_LONG, et);
      for (hle_index = 0u; hle_index < 4u; ++hle_index) {
        hle_a |= (uint32_t)READ8((uint16_t)(hle_left + hle_index)) << (8u * hle_index);
        hle_b |= (uint32_t)READ8((uint16_t)(hle_right + hle_index)) << (8u * hle_index);
      }
      iy = 3u;
      hle_nonzero = (uint8_t)fw_compare_count(hle_a - hle_b, 4u);
      hle_final_status = fw_compare_status(hle_a, hle_b, 4u, status);
      s6502_stack_ram[0x100u | sp] = hle_final_status;
      ac = hle_final_status;
      ix = hle_nonzero;
      status = hle_final_status;
      pc = POP();
      pc = (uint16_t)(pc | ((uint16_t)POP() << 8));
      pc = (uint16_t)(pc + 1u);
      goto _exit;
    }

  _hle_ebin_compare16:
    /* Dispatch guards already supply the distinct remaining cost in et. */
  _hle_ebin_compare16_suffix:
    {
      s6502_hle_compare_result_t hle_result;

      /* All internal compare checkpoints still have the caller's return
       * address at the original stack position.  Recompute the already known
       * comparison result, but charge only the exact unexecuted suffix. */
      CYCLES(et);
      S6502_HLE_RECORD(S6502_HLE_ID_COMPARE16, et);
      s6502_firmware_hle_compare16(sp, status, &hle_result);
      pc = hle_result.pc;
      ac = hle_result.ac;
      ix = hle_result.ix;
      sp = hle_result.sp;
      status = hle_result.status;
      goto _exit;
    }
