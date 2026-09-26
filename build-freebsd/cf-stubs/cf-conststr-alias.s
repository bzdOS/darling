/* ___CFConstantStringClassReference must resolve to the NSCFConstantString
 * class object ITSELF (same address): clang emits __cfstring entries whose
 * isa field stores this symbol's address. ld64.lld cannot express this with
 * -alias (it demotes the result to a local symbol), so define the alias in
 * assembly and force both symbols global via -exported_symbols_list. */
.globl ___CFConstantStringClassReference
.set ___CFConstantStringClassReference, _OBJC_CLASS_$___NSCFConstantString
