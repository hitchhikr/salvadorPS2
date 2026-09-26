            .text
            .set        push
            .set        noat
            .set        noreorder
            .globl      zx0_depacker
zx0_depacker:
            # source (fixed by the packer)
            la          $a3, 0xdeadc0de
            # dest (fixed by the packer)
            la          $a1, 0xdeadc0de
            # entry point (fixed by the packer)
            #la          $a2, 0xdeadc0de
            li          $t1, 0x80
            li          $t2, -1
literals:
            bal         get_elias
            lui         $t6, 0xFFFF
copy_lits:
            lbu         $t3, ($a3)
            addiu       $a3, $a3, 1
            sb          $t3, ($a1)
            addiu       $t0, $t0, -1
            addiu       $a1, $a1, 1
            bgtz        $t0, copy_lits
            nop
            add         $t1, $t1, $t1
            andi        $t8, $t1, 0x100
            bne         $t8, $zero, get_offset
            nop
            bal         get_elias
            nop
            addiu       $t0, $t0, -1
copy_match:
            add         $t3, $a1, $t2
            lbu         $t4, ($t3)
            sb          $t4, ($a1)
            addiu       $t0, $t0, -1
            addiu       $a1, $a1, 1
            bgez        $t0, copy_match
            nop
            add         $t1, $t1, $t1
            andi        $t8, $t1, 0x100
            beq         $t8, $zero, literals
            nop
get_offset:
            bal         elias_loop
            li          $t0, -2
            addiu       $t0, $t0, 1
            andi        $t8, $t0, 0xFF
            beq         $t8, $zero, done
            sll         $t0, $t0, 8
            andi        $t0, $t0, 0xFFFF
            and         $t2, $t2, $t6
            or          $t2, $t2, $t0
            li          $t0, 1
            lbu         $t3, ($a3)
            addiu       $a3, $a3, 1
            andi        $t3, $t3, 0xFF
            or          $t2, $t2, $t3
            andi        $t8, $t2, 1
            bne         $t8, $zero, copy_match
            sra         $t2, $t2, 1
            bal         elias_bt
            li          $v1, 0x64
            b           copy_match
            li          $a0, 0
done:
            syscall
            j           0x02345678
get_elias:
            li          $t0, 1
elias_loop:
            add         $t1, $t1, $t1
            andi        $t8, $t1, 0x100
            srl         $t8, $t8, 8
            andi        $t5, $t1, 0xff
            bne         $t5, $zero, got_bit
            nop
            lbu         $t1, ($a3)
            addiu       $a3, $a3, 1
            add         $t1, $t1, $t1
            addu        $t1, $t1, $t8
            andi        $t8, $t1, 0x100
got_bit:
            bne         $t8, $zero, got_elias
            nop
elias_bt:
            add         $t1, $t1, $t1
            andi        $t8, $t1, 0x100
            srl         $t8, $t8, 8
            add         $t0, $t0, $t0
            b           elias_loop
            addu        $t0, $t0, $t8
got_elias:
            jr          $ra
            nop
packed_data:
