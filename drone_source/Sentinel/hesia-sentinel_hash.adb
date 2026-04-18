with Interfaces;

package body Hesia_Sentinel_Hash is
   use Interfaces;
   subtype U32 is Unsigned_32;
   subtype U64 is Unsigned_64;

   function ROR (X : U32; N : Natural) return U32 is
   begin
      return Shift_Right (X, N) or Shift_Left (X, 32 - N);
   end ROR;

   function Ch (X, Y, Z : U32) return U32 is
   begin
      return (X and Y) xor ((not X) and Z);
   end Ch;

   function Maj (X, Y, Z : U32) return U32 is
   begin
      return (X and Y) xor (X and Z) xor (Y and Z);
   end Maj;

   function Big_Sigma0 (X : U32) return U32 is
   begin
      return ROR (X, 2) xor ROR (X, 13) xor ROR (X, 22);
   end Big_Sigma0;

   function Big_Sigma1 (X : U32) return U32 is
   begin
      return ROR (X, 6) xor ROR (X, 11) xor ROR (X, 25);
   end Big_Sigma1;

   function Small_Sigma0 (X : U32) return U32 is
   begin
      return ROR (X, 7) xor ROR (X, 18) xor Shift_Right (X, 3);
   end Small_Sigma0;

   function Small_Sigma1 (X : U32) return U32 is
   begin
      return ROR (X, 17) xor ROR (X, 19) xor Shift_Right (X, 10);
   end Small_Sigma1;

   function Hex_Char (N : Natural) return Character is
   begin
      if N < 10 then
         return Character'Val (Character'Pos ('0') + N);
      else
         return Character'Val (Character'Pos ('a') + (N - 10));
      end if;
   end Hex_Char;

   function Sha256_Hex (S : String) return String is
      K : constant array (0 .. 63) of U32 := (
         16#428a2f98#, 16#71374491#, 16#b5c0fbcf#, 16#e9b5dba5#,
         16#3956c25b#, 16#59f111f1#, 16#923f82a4#, 16#ab1c5ed5#,
         16#d807aa98#, 16#12835b01#, 16#243185be#, 16#550c7dc3#,
         16#72be5d74#, 16#80deb1fe#, 16#9bdc06a7#, 16#c19bf174#,
         16#e49b69c1#, 16#efbe4786#, 16#0fc19dc6#, 16#240ca1cc#,
         16#2de92c6f#, 16#4a7484aa#, 16#5cb0a9dc#, 16#76f988da#,
         16#983e5152#, 16#a831c66d#, 16#b00327c8#, 16#bf597fc7#,
         16#c6e00bf3#, 16#d5a79147#, 16#06ca6351#, 16#14292967#,
         16#27b70a85#, 16#2e1b2138#, 16#4d2c6dfc#, 16#53380d13#,
         16#650a7354#, 16#766a0abb#, 16#81c2c92e#, 16#92722c85#,
         16#a2bfe8a1#, 16#a81a664b#, 16#c24b8b70#, 16#c76c51a3#,
         16#d192e819#, 16#d6990624#, 16#f40e3585#, 16#106aa070#,
         16#19a4c116#, 16#1e376c08#, 16#2748774c#, 16#34b0bcb5#,
         16#391c0cb3#, 16#4ed8aa4a#, 16#5b9cca4f#, 16#682e6ff3#,
         16#748f82ee#, 16#78a5636f#, 16#84c87814#, 16#8cc70208#,
         16#90befffa#, 16#a4506ceb#, 16#bef9a3f7#, 16#c67178f2#
      );

      H : array (0 .. 7) of U32 := (
         16#6a09e667#, 16#bb67ae85#, 16#3c6ef372#, 16#a54ff53a#,
         16#510e527f#, 16#9b05688c#, 16#1f83d9ab#, 16#5be0cd19#
      );

      type Byte_Array is array (Positive range <>) of Unsigned_8;
      L        : constant Natural := S'Length;
      L_Bits   : constant U64 := U64 (L) * 8;
      Pad_Len  : Natural;
      Total    : Natural;
   begin
      if L = 0 then
         Pad_Len := 56 - 1;
      else
         declare
            M : Natural := (L + 1) mod 64;
         begin
            if M <= 56 then
               Pad_Len := 56 - M;
            else
               Pad_Len := 56 + 64 - M;
            end if;
         end;
      end if;
      Total := L + 1 + Pad_Len + 8;
      declare
         Msg : Byte_Array (1 .. Total);
      begin
         Msg := (others => 0);

         for I in 1 .. L loop
            Msg (I) := Unsigned_8 (Character'Pos (S (S'First + I - 1)));
         end loop;
         Msg (L + 1) := 16#80#;

         -- Append length in bits (big-endian)
         for I in 0 .. 7 loop
            Msg (Total - I) := Unsigned_8 (Shift_Right (L_Bits, I * 8) and 16#FF#);
         end loop;

         -- Process blocks
         declare
            W : array (0 .. 63) of U32;
            A, B, C, D, E, F, G, HH : U32;
         begin
            for Block in 0 .. (Total / 64) - 1 loop
               -- Prepare message schedule
               for T in 0 .. 15 loop
                  declare
                     I : Natural := Block * 64 + T * 4;
                  begin
                     W (T) :=
                       Shift_Left (U32 (Msg (I + 1)), 24) or
                       Shift_Left (U32 (Msg (I + 2)), 16) or
                       Shift_Left (U32 (Msg (I + 3)), 8) or
                       U32 (Msg (I + 4));
                  end;
               end loop;
               for T in 16 .. 63 loop
                  W (T) := Small_Sigma1 (W (T - 2)) + W (T - 7) +
                           Small_Sigma0 (W (T - 15)) + W (T - 16);
               end loop;

               A := H (0);
               B := H (1);
               C := H (2);
               D := H (3);
               E := H (4);
               F := H (5);
               G := H (6);
               HH := H (7);

               for T in 0 .. 63 loop
                  declare
                     T1 : U32 := HH + Big_Sigma1 (E) + Ch (E, F, G) + K (T) + W (T);
                     T2 : U32 := Big_Sigma0 (A) + Maj (A, B, C);
                  begin
                     HH := G;
                     G  := F;
                     F  := E;
                     E  := D + T1;
                     D  := C;
                     C  := B;
                     B  := A;
                     A  := T1 + T2;
                  end;
               end loop;

               H (0) := H (0) + A;
               H (1) := H (1) + B;
               H (2) := H (2) + C;
               H (3) := H (3) + D;
               H (4) := H (4) + E;
               H (5) := H (5) + F;
               H (6) := H (6) + G;
               H (7) := H (7) + HH;
            end loop;
         end;
      end;

      declare
         R   : String (1 .. 64);
         Pos : Natural := 1;
         Mask : constant U32 := 16#F#;
      begin
         for I in 0 .. 7 loop
            for N in reverse 0 .. 7 loop
               declare
                  V : U32 := Shift_Right (H (I), N * 4) and Mask;
               begin
                  R (Pos) := Hex_Char (Natural (V));
                  Pos := Pos + 1;
               end;
            end loop;
         end loop;
         return R;
      end;
   end Sha256_Hex;
end Hesia_Sentinel_Hash;
