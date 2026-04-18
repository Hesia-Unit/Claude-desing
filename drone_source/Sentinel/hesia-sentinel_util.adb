with Ada.Characters.Handling;
with Ada.Directories;
with Ada.Streams.Stream_IO;
with Ada.Strings.Fixed;
with Ada.Strings.Unbounded;
with Ada.Text_IO;

package body Hesia_Sentinel_Util is
   function Trim (S : String) return String is
   begin
      return Ada.Strings.Fixed.Trim (S, Ada.Strings.Both);
   end Trim;

   function Starts_With (S, Prefix : String) return Boolean is
   begin
      if S'Length < Prefix'Length then
         return False;
      end if;
      return S (S'First .. S'First + Prefix'Length - 1) = Prefix;
   end Starts_With;

   function To_Lower (S : String) return String is
      R : String (S'Range);
   begin
      for I in S'Range loop
         R (I) := Ada.Characters.Handling.To_Lower (S (I));
      end loop;
      return R;
   end To_Lower;

   function Read_Text_File (Path : String; Max : Natural := 4096) return String is
      use Ada.Strings.Unbounded;
      F    : Ada.Text_IO.File_Type;
      Acc  : Unbounded_String := To_Unbounded_String ("");
      Size : Natural := 0;
   begin
      Ada.Text_IO.Open (F, Ada.Text_IO.In_File, Path);
      while not Ada.Text_IO.End_Of_File (F) loop
         declare
            Line : constant String := Ada.Text_IO.Get_Line (F);
         begin
            if Size + Line'Length + 1 > Max then
               declare
                  Keep : Natural := Max - Size;
               begin
                  if Keep > 0 then
                     Append (Acc, Line (Line'First .. Line'First + Keep - 1));
                  end if;
               end;
               exit;
            end if;
            Append (Acc, Line);
            Append (Acc, ASCII.LF);
            Size := Size + Line'Length + 1;
         end;
      end loop;
      Ada.Text_IO.Close (F);
      return To_String (Acc);
   exception
      when others =>
         return "";
   end Read_Text_File;

   function Read_Bytes_File (Path : String; Max : Natural := 4096) return String is
      use Ada.Streams;
      F    : Stream_IO.File_Type;
      Buf  : Stream_Element_Array (1 .. Stream_Element_Offset (Max));
      Last : Stream_Element_Offset := 0;
      Len  : Natural := 0;
   begin
      Stream_IO.Open (F, Stream_IO.In_File, Path);
      Stream_IO.Read (F, Buf, Last);
      Stream_IO.Close (F);
      Len := Natural (Last);
      if Len = 0 then
         return "";
      end if;
      declare
         S : String (1 .. Len);
      begin
         for I in 1 .. Len loop
            S (I) := Character'Val (Integer (Buf (Stream_Element_Offset (I))));
         end loop;
         return S;
      end;
   exception
      when others =>
         return "";
   end Read_Bytes_File;

   function File_Exists (Path : String) return Boolean is
   begin
      return Ada.Directories.Exists (Path);
   exception
      when others =>
         return False;
   end File_Exists;
end Hesia_Sentinel_Util;
